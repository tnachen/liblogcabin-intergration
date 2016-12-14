#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>

#include <liblogcabin/Core/Config.h>
#include <liblogcabin/Core/Debug.h>
#include <liblogcabin/Raft/RaftConsensus.h>

#include <unistd.h>

using namespace LibLogCabin;

class LibLogcabinHandler : public proxygen::RequestHandler {
public:
  explicit LibLogcabinHandler(std::shared_ptr<Raft::RaftConsensus> raft)
      : raft_(raft)
  {

  }

  void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers)
      noexcept override {}

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
    if (body_) {
      body_->prependChain(std::move(body));
    } else {
      body_ = std::move(body);
    }
  }

  void onEOM() noexcept override {
    ResponseBuilder(downstream_)
      .status(200, "OK")
      .body(std::move(body_))
      .sendWithEOM();
  }

  void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override {}

  void requestComplete() noexcept override {
    delete this;
  }

  void onError(proxygen::ProxygenError err) noexcept override {
    delete this;
  }

 private:
  std::unique_ptr<folly::IOBuf> body_;
  std::shared_ptr<Raft::RaftConsensus> raft_;
};

class LibLogcabinRequestHandlerFactory : public RequestHandlerFactory {
public:
  LibLogcabinRequestHandlerFactory(Core::Config& config, uint64_t serverId) :
      raft(new RaftConsensus(config, serverId))
  {
    raft->init();
    if (serverId == 1) {
      raft->bootstrapConfiguration();
      bool elected = false;
      while (!elected) {
        elected = raft->getLastCommitIndex().first == RaftConsensus::ClientResult::SUCCESS;
        usleep(1000);
      }
    }
  }

  void onServerStart(folly::EventBase* evb) noexcept override {
  }

  void onServerStop() noexcept override {
  }

  RequestHandler* onRequest(RequestHandler*, HTTPMessage*) noexcept override {
    return new LibLogCabinRequestHandler(raft);
  }

 private:
  std::shared_ptr<LibLogcabin::RaftConsensus> raft;
};

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "raft-example-server <port>";
    return 1;
  }

  std::vector<HTTPServer::IPConfig> IPs = {
    {SocketAddress(FLAGS_ip, FLAGS_http_port, true), Protocol::HTTP},
  };

  HTTPServerOptions options;
  options.threads = static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN));
  options.idleTimeout = std::chrono::milliseconds(60000);
  options.shutdownOn = {SIGINT, SIGTERM};
  options.enableContentCompression = false;
  options.handlerFactories = RequestHandlerChain()
                 .addThen<LibLogCabinRequestHandlerFactory>()
                 .build();

  HTTPServer server(std::move(options));
  server.bind(IPs);

  // Start HTTPServer mainloop in a separate thread
  std::thread t([&] () {
    server.start();
  });

  t.join();
  return 0;
}
