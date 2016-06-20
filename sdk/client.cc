#include "client.h"

#include <unistd.h>
#include <getopt.h>
#include <algorithm>
#include "logger.h"
#include "bada_sdk.pb.h"

namespace floyd {
namespace client {

void Tokenize(const std::string& str, std::vector<std::string>& tokens, const char& delimiter = ' ') {
  size_t prev_pos = str.find_first_not_of(delimiter, 0);
  size_t pos = str.find(delimiter, prev_pos);

  while (prev_pos != std::string::npos || pos != std::string::npos) {
    std::string token(str.substr(prev_pos, pos - prev_pos));
    //printf ("find a token(%s), prev_pos=%u pos=%u\n", token.c_str(), prev_pos, pos);
    tokens.push_back(token);

    prev_pos = str.find_first_not_of(delimiter, pos);
    pos = str.find_first_of(delimiter, prev_pos);
  }
}

///// Server //////
Server::Server(const std::string& str) {
  size_t pos = str.find(':');
  ip = str.substr(0, pos);
  port = atoi(str.substr(pos + 1).c_str());
}

///// Option //////
Option::Option()
    : timeout(1000) {
    }

Option::Option(const std::string& server_str) 
  : timeout(1000) {
  std::vector<std::string> server_list;
  Tokenize(server_str, server_list, ',');
  Init(server_list);
}

Option::Option(const std::vector<std::string>& server_list)
  : timeout(1000) {
  Init(server_list); 
}

Option::Option(const Option& option)
  : timeout(option.timeout) {
    std::copy(option.servers.begin(), option.servers.end(), std::back_inserter(servers));
  }


void Option::Init(const std::vector<std::string>& server_list) {
  for (auto it = server_list.begin(); it != server_list.end(); it++) {
    servers.push_back(Server(*it));
  }
}

void Option::ParseFromArgs(int argc, char *argv[]) {
  if (argc < 2) {
    LOG_ERROR("invalid arguments!");
    abort();
  }

  static struct option const long_options[] = {
    {"server", required_argument, NULL, 's'},
    {NULL, 0, NULL, 0} };

  std::string server_str;
  int opt, optindex;
  while ((opt = getopt_long(argc, argv, "s:", long_options, &optindex)) != -1) {
    switch (opt) {
      case 's':
        server_str = optarg;
        break;
      default:
        break;
    }
  }

  std::vector<std::string> server_list;

  Tokenize(server_str, server_list, ',');
  Init(server_list);
}

////// Cluster //////
Cluster::Cluster(const Option& option)
  : option_(option),
  pb_cli_(new BadaPbCli) {
  Init();
}

void Cluster::Init() {
  if (option_.servers.size() < 1) {
    LOG_ERROR("cluster has no server!");
    abort();
  }
  // TEST use the first server
  pink::Status result = pb_cli_->Connect(option_.servers[0].ip, option_.servers[0].port);
  if (!result.ok()) {
    LOG_ERROR("cluster connect error, %s", result.ToString().c_str());
  }
}

Status Cluster::Put(const std::string& key, const std::string& value) {
  // TEST use bada SDKSet
  SdkSet* sdk_set = new SdkSet;

  pb_cli_->opcode_ = 513;
  sdk_set->set_opcode(513);
  sdk_set->set_table("table");
  sdk_set->set_key(key.data(), key.size());
  sdk_set->set_value(value.data(), value.size());
  sdk_set->set_writesrc(0);

  pink::Status result = pb_cli_->Send(sdk_set);
  if (!result.ok()) {
    LOG_ERROR("Send error: %s", result.ToString().c_str());
    return Status::IOError("Send failed, " + result.ToString());
  }

  SdkSetRet sdk_set_ret;
  result = pb_cli_->Recv(&sdk_set_ret);
  if (!result.ok()) {
    LOG_ERROR("Recv error: %s", result.ToString().c_str());
    return Status::IOError("Recv failed, " + result.ToString());
  }

  LOG_INFO("Put OK, opcode is %d, status is %d\n", sdk_set_ret.opcode(), sdk_set_ret.status());
  return Status::OK();
}

////// BadaPbCli //////
void BadaPbCli::BuildWbuf() {
  uint32_t len;
  wbuf_len_ = msg_->ByteSize();
  len = htonl(wbuf_len_ + 4);
  memcpy(wbuf_, &len, sizeof(uint32_t));
  len = htonl(opcode_);
  memcpy(wbuf_ + 4, &len, sizeof(uint32_t));
  msg_->SerializeToArray(wbuf_ + COMMAND_HEADER_LENGTH + 4, wbuf_len_);
  wbuf_len_ += COMMAND_HEADER_LENGTH + 8;

  printf ("wbuf_[0-4]  bytesize=%d len=%d\n", wbuf_len_, len);
}

} // namespace client
} // namspace floyd