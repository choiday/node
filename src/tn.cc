#include "tn.h"
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include "env.h"
#include "inspector_agent.h"
#include "node_binding.h"
#include "node_native_module_env.h"
#include "node_options.h"
#include "tracing/trace_event.h"
#include "util.h"
#include "v8.h"

namespace titan {
using namespace node;
using namespace node::native_module;
using namespace v8;

int argc = 3;
int exec_argc = 0;
const char* prog_name[] = {"titan", "--inspect", "./scripts/main.js"};
const char** argv = prog_name;
const char** exec_argv = nullptr;
struct NodeBindings::PImpl {
  // Create the environment and load node.js.
  void CreateEnvironment(v8::Local<v8::Context> context,
                         node::MultiIsolatePlatform* platform);

  // Current thread's libuv loop.
  uv_loop_t* uv_loop_;

  node::Environment* env_ = nullptr;

  // Isolate data used in creating the environment
  node::IsolateData* isolate_data_ = nullptr;

  std::function<void(const std::string& logMessage)> logger_;
};
NodeBindings& NodeBindings::getInstance() {
  static NodeBindings* instance = new NodeBindings();
  return *instance;
}
NodeBindings::NodeBindings() : m_impl(new PImpl) {
  m_impl->uv_loop_ = uv_default_loop();

  node::Init(&argc, argv, &exec_argc, &exec_argv);

  if (!node::tracing::TraceEventHelper::GetAgent())
    node::tracing::TraceEventHelper::SetAgent(new node::tracing::Agent());
}
NodeBindings::~NodeBindings() {}

void NodeBindings::initializeContext(v8::Local<v8::Context> context) {
  bool initialized = node::InitializeContext(context);
  m_impl->CreateEnvironment(context, nullptr);
}
v8::Local<v8::Context> NodeBindings::getContext() {
  return m_impl->env_->context();
}
void NodeBindings::update() {
  node::Environment* env = m_impl->env_;
  if (!env) return;

  v8::HandleScope handle_scope(env->isolate());

  // Enter node context while dealing with uv events.
  v8::Context::Scope context_scope(env->context());

  // Perform microtask checkpoint after running JavaScript.
  v8::MicrotasksScope script_scope(env->isolate(),
                                   v8::MicrotasksScope::kRunMicrotasks);

  v8::TryCatch __trycatch(env->isolate());
  // Deal with uv events.
  int r = uv_run(m_impl->uv_loop_, UV_RUN_NOWAIT);

  if (__trycatch.HasCaught()) {
    v8::String::Utf8Value ascii(env->isolate(), __trycatch.Message()->Get());
    v8::String::Utf8Value stackMessage(
        env->isolate(), __trycatch.StackTrace(env->context()).ToLocalChecked());
    if (m_impl->logger_)
    {
      m_impl->logger_(std::string(*stackMessage, stackMessage.length()));
    }
  }
}
void NodeBindings::registerLogCallback(
    std::function<void(const std::string& logMessage)> callbackFn) {
  m_impl->logger_ = callbackFn;
}

// Create the environment and load node.js.
void NodeBindings::PImpl::CreateEnvironment(
    v8::Local<v8::Context> context, node::MultiIsolatePlatform* platform) {
  isolate_data_ =
      node::CreateIsolateData(context->GetIsolate(), uv_loop_, platform);
  node::Environment* env = node::CreateEnvironment(
      isolate_data_, context, argc, argv, 0, nullptr, false);

  v8::HandleScope handle_scope(env->isolate());
  v8::Context::Scope context_scope(env->context());
  // Node uses the deprecated SetAutorunMicrotasks(false) mode, we should
  // switch to use the scoped policy to match blink's behavior.
  context->GetIsolate()->SetMicrotasksPolicy(v8::MicrotasksPolicy::kScoped);

  node::DebugOptions options;
  std::vector<std::string> args;
  std::vector<std::string> exec_args;
  std::vector<std::string> v8_args;
  std::vector<std::string> errors;

  for (auto& arg : prog_name) {
    args.push_back(arg);
  }

  node::options_parser::Parse(&args,
                              &exec_args,
                              &v8_args,
                              &options,
                              node::options_parser::kDisallowedInEnvironment,
                              &errors);

  auto* inspector = env->inspector_agent();
  const char* path = "";
  options.inspector_enabled = true;
  inspector->Start(
      path, options, std::make_shared<node::HostPort>(options.host_port), true);

  v8::TryCatch __trycatch(env->isolate());

  node::BootstrapEnvironment(env);
  node::LoadEnvironment(env);

  if (__trycatch.HasCaught()) {
    v8::String::Utf8Value ascii(env->isolate(), __trycatch.Message()->Get());
    v8::String::Utf8Value stackMessage(
        env->isolate(), __trycatch.StackTrace(context).ToLocalChecked());

    if (logger_) {
      logger_(std::string(*stackMessage, stackMessage.length()));
    }

  }

  env_ = env;
}
}  // namespace titan
