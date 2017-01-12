#include <string>
#include <vector>

#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/ResponseBuilder.h>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>

#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/SocketAddress.h>

#include <proxygen/lib/http/HTTPConnector.h>
#include <proxygen/httpclient/samples/curl/CurlClient.h>

#include <liblogcabin/Core/Config.h>
#include <liblogcabin/Core/Debug.h>
#include <liblogcabin/Raft/RaftConsensus.h>

#include <unistd.h>

using namespace LibLogCabin;
using namespace proxygen;

using folly::EventBase;
using folly::EventBaseManager;
using folly::SocketAddress;

using HTTPProtocol = HTTPServer::Protocol;

class LibLogCabinHandler : public RequestHandler {
public:
  explicit LibLogCabinHandler(std::shared_ptr<Raft::RaftConsensus> raft)
      : isJoinCluster(false), body_(), raft_(raft)
  {

  }

  void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers)
      noexcept override
  {
    if (headers->getPath() == "/join") {
      isJoinCluster = true;
    }
  }

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
    if (body_) {
      body_->prependChain(std::move(body));
    } else {
      body_ = std::move(body);
    }
  }

  std::string joinCluster() {
      const char* p = reinterpret_cast<const char*>(body_->data());
      std::string data(p, body_->length());
      std::cout << "Received POST: " << data << std::endl;
      auto parts = split(data, ',');

      if (parts.size() != 2) {
        return "Invalid server format";
      }

      LibLogCabin::Raft::Protocol::SimpleConfiguration configuration;
      uint64_t lastId;
      auto getResult = raft_->getConfiguration(configuration, lastId);
      if (getResult != LibLogCabin::Raft::RaftConsensus::ClientResult::SUCCESS) {
        return "Unable to get configuration";
      }

      LibLogCabin::Protocol::Client::SetConfiguration::Request setRequest;
      setRequest.set_old_id(lastId);
      for (int i = 0; i < configuration.servers().size(); ++i) {
        auto existingServer = configuration.servers(i);
        LibLogCabin::Protocol::Client::Server server;
        server.set_server_id(existingServer.server_id());
        server.set_addresses(existingServer.addresses());
        setRequest.add_new_servers()->CopyFrom(server);
      }

      // Add the new server.
      LibLogCabin::Protocol::Client::Server server;
      server.set_server_id(atoi(parts[0].c_str()));
      server.set_addresses(parts[1]);
      setRequest.add_new_servers()->CopyFrom(server);

      LibLogCabin::Protocol::Client::SetConfiguration::Response setResponse;
      auto result = raft_->setConfiguration(setRequest, setResponse);
      if (result != Raft::RaftConsensus::ClientResult::SUCCESS) {
        return "Unable to set configuration";
      }

      return "";
  }

  std::string writeData() {
    const char* p = reinterpret_cast<const char*>(body_->data());
    std::string data(p, body_->length());
    LibLogCabin::Core::Buffer buffer;
    std::cout << "Writing data '" << data << "' to log..." << std::endl;
    buffer.setData(const_cast<char*>(data.data()), sizeof(int) * data.size(), NULL);
    Raft::RaftConsensus::ClientResult result;
    int newIndex;
    std::tie(result, newIndex) = raft_->replicate(buffer);
    if (result != Raft::RaftConsensus::ClientResult::SUCCESS) {
      return "Push data failed";
    }

    return "";
  }

  void onEOM() noexcept override {
    if (isJoinCluster) {
      auto error = joinCluster();

      if (error != "") {
        std::cerr << "Error when joining cluster: " << error << std::endl;
        ResponseBuilder(downstream_)
          .status(500, "Internal Server Error")
          .body(error)
          .sendWithEOM();
      }
    } else {
      auto error = writeData();
      if (error != "") {
        std::cerr << "Error when writing data: " << error << std::endl;
        ResponseBuilder(downstream_)
          .status(500, "Internal Server Error")
          .body(error)
          .sendWithEOM();
      }
    }

    ResponseBuilder(downstream_)
      .status(202, "Accepted")
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
  std::vector<std::string> split(const std::string &text, char sep) {
    std::vector<std::string> tokens;
    std::size_t start = 0, end = 0;
    while ((end = text.find(sep, start)) != std::string::npos) {
      tokens.push_back(text.substr(start, end - start));
      start = end + 1;
    }
    tokens.push_back(text.substr(start));
    return tokens;
  }


  bool isJoinCluster;
  std::unique_ptr<folly::IOBuf> body_;
  std::shared_ptr<Raft::RaftConsensus> raft_;
};

class LibLogCabinRequestHandlerFactory : public RequestHandlerFactory {
public:
  LibLogCabinRequestHandlerFactory(
    const std::shared_ptr<Raft::RaftConsensus>& raft)				  
    : raft(raft)
  {
  }

  void onServerStart(folly::EventBase* evb) noexcept override {
  }

  void onServerStop() noexcept override {
  }

  RequestHandler* onRequest(RequestHandler*, HTTPMessage*) noexcept override {
    return new LibLogCabinHandler(raft);
  }

 private:
  std::shared_ptr<Raft::RaftConsensus> raft;
  int serverId;
  std::string listenAddress;
  std::string leaderAddress;
};

void initRaft(
    const std::string& leaderAddress,
    const std::string& listenAddress,
    int serverId,
    const std::shared_ptr<Raft::RaftConsensus>& raft)
{
    raft->init();

    if (leaderAddress != "") {
      folly::EventBase innerevb;
      HTTPHeaders headers;
      URL url(leaderAddress + "/join");
      folly::StringPiece data(std::to_string(serverId) + std::string(",") + listenAddress);
      std::string path = "/tmp/raft-example-server-" + std::to_string(::getpid() + rand());
      if (!folly::writeFile(data, path.c_str())) {
        std::cerr << "Unable to write to temp file" << std::endl;
        exit(1);
      }

          // Join the leader
      CurlService::CurlClient curl(
          &innerevb, HTTPMethod::POST, url, headers, path);
      curl.setFlowControlSettings(64 * 1024);
      curl.setLogging(true);

      folly::HHWheelTimer::UniquePtr timer{new folly::HHWheelTimer(
          &innerevb,
          std::chrono::milliseconds(folly::HHWheelTimer::DEFAULT_TICK_INTERVAL),
          folly::AsyncTimeout::InternalEnum::NORMAL,
          std::chrono::milliseconds(1000))};
      HTTPConnector connector(&curl, timer.get());
      SocketAddress addr(url.getHost(), url.getPort(), true);
      connector.connect(&innerevb,
                        addr,
                        std::chrono::milliseconds(1000));
      innerevb.loop();
      auto response = curl.getResponse();
      auto code = response->getStatusCode();
      if (code != 202) {
        std::cerr << "Unable to join leader, returned status code : "
                 << code << std::endl;
        exit(1);
      }
    } else {
      // We are the leader.
      raft->bootstrapConfiguration();
      bool elected = false;
      while (!elected) {
        elected = raft->getLastCommitIndex().first == Raft::RaftConsensus::ClientResult::SUCCESS;
        usleep(1000);
      }
    }
}


int main(int argc, char** argv) {
  if (argc < 4) {
    std::cout << "raft-example-server <id> <http_port> <listenAddress> <leaderAddress>" << std::endl;
    return 1;
  }

  int serverId = atoi(argv[1]);

  std::vector<HTTPServer::IPConfig> IPs = {
    {SocketAddress("0.0.0.0", atoi(argv[2]), true), HTTPProtocol::HTTP},
  };

  std::string listenAddress = std::string(argv[3]);

  std::string leaderAddress;
  if (argc > 4) {
    leaderAddress = std::string(argv[4]);
  }

  Core::Config config;
  config.set("listenAddresses", listenAddress);

  std::shared_ptr<Raft::RaftConsensus> raft(new Raft::RaftConsensus(config, serverId));
  initRaft(leaderAddress, listenAddress, serverId, raft);
  
  HTTPServerOptions options;
  options.threads = static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN));
  options.idleTimeout = std::chrono::milliseconds(60000);
  options.shutdownOn = {SIGINT, SIGTERM};
  options.enableContentCompression = false;
  options.handlerFactories = RequestHandlerChain()
    .addThen<LibLogCabinRequestHandlerFactory>(raft)
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
