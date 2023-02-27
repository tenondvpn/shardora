#include "dht/dht_utils.h"

#include "protos/dht.pb.h"
#include "dht/dht_key.h"

namespace zjchain {

namespace dht {

Node::Node() {}

Node::Node(
        int32_t in_sharding_id,
        const std::string& in_public_ip,
        uint16_t in_public_port,
        const std::string& in_pubkey_str,
        const std::string& in_id) {
    sharding_id = in_sharding_id;
    DhtKeyManager dhtkey(sharding_id, in_pubkey_str);
    dht_key = dhtkey.StrKey();
    dht_key_hash = common::Hash::Hash64(dht_key);
    public_ip = in_public_ip;
    public_port = in_public_port;
    pubkey_str = in_pubkey_str;
    assert(!pubkey_str.empty());
    id = in_id;
    id_hash = common::Hash::Hash64(id);
}

int DefaultDhtSignCallback(
        const std::string& peer_pubkey,
        const std::string& append_data,
        std::string* enc_data,
        std::string* sign_ch,
        std::string* sign_re) {
    /*  std::string sec_key;
      security::PublicKey pubkey(peer_pubkey);
      if (security::EcdhCreateKey::Instance()->CreateKey(
              pubkey,
              sec_key) != security::kSecuritySuccess) {
          return dht::kDhtError;
      }

      auto now_tm_sec = std::chrono::steady_clock::now().time_since_epoch().count() /
          1000000000llu;
      std::string enc_src_data = std::to_string(now_tm_sec);
      uint32_t data_size = (enc_src_data.size() / AES_BLOCK_SIZE) * AES_BLOCK_SIZE + AES_BLOCK_SIZE;
      char* tmp_out_enc = (char*)malloc(data_size);
      memset(tmp_out_enc, 0, data_size);
      if (security::Aes::Encrypt(
              (char*)enc_src_data.c_str(),
              enc_src_data.size(),
              (char*)sec_key.c_str(),
              sec_key.size(),
              tmp_out_enc) != security::kSecuritySuccess) {
          free(tmp_out_enc);
          return dht::kDhtError;
      }

      *enc_data = std::string(tmp_out_enc, data_size);
      free(tmp_out_enc);
      security::Signature sign;
      bool sign_res = security::Security::Instance()->Sign(
          *enc_data,
          *(security::Security::Instance()->prikey()),
          *(security::Security::Instance()->pubkey()),
          sign);
      if (!sign_res) {
          return dht::kDhtError;
      }

      std::string sign_challenge_str;
      std::string sign_response_str;
      sign.Serialize(sign_challenge_str, sign_response_str);
      *sign_ch = sign_challenge_str;
      *sign_re = sign_response_str;*/
    return dht::kDhtSuccess;
}

}  // namespace dht

}  // namespace zjchain
