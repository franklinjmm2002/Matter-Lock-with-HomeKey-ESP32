#include <HK_HomeKit.h>
#include "TLV8.hpp"
#include "logging.h"
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha1.h>
#if defined(CONFIG_IDF_CMAKE)
#include <esp_random.h>
#else
#include <sodium.h>
#endif
#include <mbedtls/error.h>
#include <vector>
#include "fmt/base.h"
#include <fmt/ranges.h>

HK_HomeKit::HK_HomeKit(readerData_t& readerData, std::function<void(const readerData_t&)> save_cb, std::function<void()> remove_key_cb, std::vector<uint8_t>& tlvData) : tlvData(tlvData), readerData(readerData), save_cb(save_cb), remove_key_cb(remove_key_cb) { }

namespace {
std::vector<uint8_t> make_status_response(uint8_t response_tag, uint8_t status_tag, uint8_t status_value)
{
  // Avoid nested TLV8 packing for the simplest status-only response path.
  // Apple expects an outer response TLV whose payload is a single status TLV.
  return std::vector<uint8_t>{
    response_tag,
    0x03,
    status_tag,
    0x01,
    status_value,
  };
}
}  // namespace

std::vector<uint8_t> HK_HomeKit::processResult() {
  tlv_it operation;
  tlv_it RKR;
  tlv_it DCR;
  TLV8 rxTlv;
  if (tlvData.empty()) {
    LOG(E, "Received empty Home Key TLV request");
    return std::vector<uint8_t>();
  }

  rxTlv.parse(tlvData.data(), tlvData.size());
  operation = rxTlv.find(kReader_Operation);
  RKR = rxTlv.find(kReader_Reader_Key_Request);
  DCR = rxTlv.find(kReader_Device_Credential_Request);
  if (operation == rxTlv.end() || operation->length() == 0 || operation->data() == nullptr) {
    LOG(E, "Home Key TLV request is missing operation tag");
    return std::vector<uint8_t>();
  }

  const uint8_t operation_type = *operation->data();
  LOG(I, "TLV OPERATION: %d", operation_type);

  if (operation_type == kReader_Operation_Read) {
      if (RKR != rxTlv.end() && RKR->tag == kReader_Reader_Key_Request) {
        LOG(I,"GET READER KEY REQUEST");
        if (readerData.reader_sk.size() > 0) {
          TLV8 getResSub;
          getResSub.add(kReader_Res_Key_Identifier, readerData.reader_gid);
          std::vector<uint8_t> subTlv = getResSub.get();
          TLV8 getResTlv;
          getResTlv.add(kReader_Res_Reader_Key_Response, subTlv);
          std::vector<uint8_t> tlvRes = getResTlv.get();
          return tlvRes;
        }
        return std::vector<uint8_t>{  };
      }
      LOG(W, "Unsupported Home Key read request");
      return std::vector<uint8_t>();
  }

  if (operation_type == kReader_Operation_Write) {
    if (RKR != rxTlv.end()) {
      LOG(I,"TLV RKR: %d", RKR->length());
      LOG(I,"SET READER KEY REQUEST");
      int ret = set_reader_key(RKR->value);
      if (ret == 0) {
        LOG(I,"READER KEY SAVED TO NVS, COMPOSING RESPONSE");
        return make_status_response(kReader_Res_Reader_Key_Response, kReader_Res_Status, 0);
      }
      LOG(E, "Failed to store reader key from Home Key request");
      return make_status_response(kReader_Reader_Key_Response, kReader_Res_Status, OUT_OF_RESOURCES);
    }
    else if (DCR != rxTlv.end()) {
      LOG(I,"TLV DCR: %d",DCR->length());
      LOG(I,"PROVISION DEVICE CREDENTIAL REQUEST");
      if (readerData.issuers.empty()) {
        LOG(W, "Rejecting device credential request because no issuers are provisioned yet");
        return make_status_response(kDevice_Credential_Response, kDevice_Res_Status, DOES_NOT_EXIST);
      }
      LOG(I, "Step 0: Calling provision_device_cred");
      auto state = provision_device_cred(DCR->value);
      LOG(I, "Step 8: Returned from provision_device_cred");
      if (std::get<1>(state) == DOES_NOT_EXIST || std::get<0>(state).empty()) {
        LOG(W, "Device credential request could not be satisfied");
        return make_status_response(kDevice_Credential_Response, kDevice_Res_Status, DOES_NOT_EXIST);
      }
      if (std::get<1>(state) != 99) {
        LOG(I, "Step 9: Adding SubTlv");
        TLV8 dcrResSubTlv;
        dcrResSubTlv.add(kDevice_Res_Issuer_Key_Identifier, std::get<0>(state));
        
        uint8_t status_val = std::get<1>(state);
        dcrResSubTlv.add(kDevice_Res_Status, status_val);

        LOG(I, "Step 10: Packing SubTlv (calling get)");
        for (const auto& item : dcrResSubTlv) {
            LOG(I, "DEBUG: item tag: %d, length: %zu", item.tag, item.length());
        }
        std::vector<uint8_t> packedRes = dcrResSubTlv.get();
        LOG(I, "Step 10.1: packedRes created, size: %zu", packedRes.size());
        TLV8 dcrResTlv;
        LOG(I, "Step 10.2: dcrResTlv created");
        dcrResTlv.add(kDevice_Credential_Response, packedRes);
        LOG(I, "Step 10.3: added kDevice_Credential_Response");
        LOG(I, "Step 11: Packing ResTlv (calling get)");
        std::vector<uint8_t> result = dcrResTlv.get();
        LOG(I, "Step 11.1: result created, size: %zu", result.size());
        LOG(I, "Step 12: Returning result");
        return result;
      }
      LOG(W, "Device credential request returned no result");
      return make_status_response(kDevice_Credential_Response, kDevice_Res_Status, NOT_SUPPORTED);
    }
    LOG(W, "Unsupported Home Key write request");
    return make_status_response(kReader_Reader_Key_Response, kReader_Res_Status, NOT_SUPPORTED);
  }

  if (operation_type == kReader_Operation_Remove) {
    if (RKR != rxTlv.end()) {
      LOG(I,"REMOVE READER KEY REQUEST");
      readerData.reader_gid.clear();
      readerData.reader_id.clear();
      readerData.reader_sk.clear();
      save_cb(readerData);
      return std::vector<uint8_t>{ 0x7, 0x3, 0x2, 0x1, 0x0 };
    }
    LOG(W, "Unsupported Home Key remove request");
    return make_status_response(kReader_Reader_Key_Response, kReader_Res_Status, DOES_NOT_EXIST);
  }

  LOG(W, "Unknown Home Key operation type: %u", operation_type);
  return std::vector<uint8_t>();
}

int HK_HomeKit::esp_rng(void*, uint8_t* buf, size_t len)
{
  #ifdef CONFIG_IDF_CMAKE
  esp_fill_random(buf, len);
  #else
  randombytes(buf, len);
  #endif
  return 0;
}

std::vector<uint8_t> HK_HomeKit::get_x(std::vector<uint8_t> &pubKey)
{
  if (pubKey.size() >= 33 && pubKey[0] == 0x04) {
    return std::vector<uint8_t>(pubKey.begin() + 1, pubKey.begin() + 33);
  }
  return std::vector<uint8_t>();
}

std::vector<uint8_t> HK_HomeKit::getPublicKey(uint8_t *privKey, size_t len)
{
  mbedtls_ecp_keypair keypair;
  mbedtls_ecp_keypair_init(&keypair);
  int ecp_key = mbedtls_ecp_read_key(MBEDTLS_ECP_DP_SECP256R1, &keypair, privKey, len);
  int ret = mbedtls_ecp_mul(&keypair.private_grp, &keypair.private_Q, &keypair.private_d, &keypair.private_grp.G, esp_rng, NULL);
  if(ecp_key != 0){
    LOG(E, "ecp_write_1 - %d", ecp_key);
    return std::vector<uint8_t>();
  }
  if (ret != 0) {
    LOG(E, "mbedtls_ecp_mul - %d", ret);
    return std::vector<uint8_t>();
  }
    size_t olenPub = 0;
  std::vector<uint8_t> readerPublicKey(MBEDTLS_ECP_MAX_PT_LEN);
  mbedtls_ecp_point_write_binary(&keypair.private_grp, &keypair.private_Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olenPub, readerPublicKey.data(), readerPublicKey.capacity());
  readerPublicKey.resize(olenPub);

  // Cleanup
  mbedtls_ecp_keypair_free(&keypair);
  return readerPublicKey;
}

std::vector<uint8_t> HK_HomeKit::getHashIdentifier(const std::vector<uint8_t>& key, bool sha256) {
  // ESP_LOGV(TAG, "Key: {}, Length: {}, sha256?: {}", bufToHexString(key.data(), key.size()).c_str(), key.size(), sha256);
  std::vector<unsigned char> hashable;
  if (sha256) {
    std::string string = "key-identifier";
    hashable.insert(hashable.begin(), string.begin(), string.end());
  }
  hashable.insert(hashable.end(), key.begin(), key.end());
  // ESP_LOGV(TAG, "Hashable: {}", bufToHexString(&hashable.front(), hashable.size()).c_str());
  std::vector<uint8_t> hash(32);
  if (sha256) {
    mbedtls_sha256(&hashable.front(), hashable.size(), hash.data(), 0);
  }
  else {
    mbedtls_sha1(&hashable.front(), hashable.size(), hash.data());
  }
  // ESP_LOGD(TAG, "HashIdentifier: {}", bufToHexString(hash.data(), 32).c_str());
  return hash;
}

std::tuple<std::vector<uint8_t>, int> HK_HomeKit::provision_device_cred(std::vector<uint8_t> buf) {
  TLV8 dcrTlv;
  dcrTlv.parse(buf.data(), buf.size());
  hkIssuer_t* foundIssuer = nullptr;
  tlv_it tlvIssuerId = dcrTlv.find(kDevice_Req_Issuer_Key_Identifier);
  if (tlvIssuerId == dcrTlv.end() || tlvIssuerId->length() == 0 || tlvIssuerId->data() == nullptr) {
    LOG(E, "Device credential request missing issuer identifier");
    return std::make_tuple(readerData.reader_gid, DOES_NOT_EXIST);
  }
  std::vector<uint8_t> issuerIdentifier = tlvIssuerId->value;
  if (issuerIdentifier.size() > 0) {
    if (readerData.issuers.empty()) {
      LOG(W, "Device credential request received before any issuer was provisioned");
      return std::make_tuple(issuerIdentifier, DOES_NOT_EXIST);
    }
    for (auto& issuer : readerData.issuers) {
      if (issuer.issuer_id.size() == issuerIdentifier.size() &&
          std::equal(issuer.issuer_id.begin(), issuer.issuer_id.end(), issuerIdentifier.begin())) {
        LOG(I, "Found issuer with identifier length %zu", issuer.issuer_id.size());
        foundIssuer = &issuer;
      }
    }
    if (foundIssuer != nullptr) {
      hkEndpoint_t* foundEndpoint = 0;
      tlv_it tlvDevicePubKey = dcrTlv.find(kDevice_Req_Public_Key);
      if (tlvDevicePubKey == dcrTlv.end() || tlvDevicePubKey->length() == 0 || tlvDevicePubKey->data() == nullptr) {
        LOG(E, "Device credential request missing public key");
        return std::make_tuple(issuerIdentifier, DOES_NOT_EXIST);
      }
      std::vector<uint8_t> devicePubKey = tlvDevicePubKey->value;
      if (devicePubKey.size() != 64) {
        LOG(E, "Device credential request public key has unexpected size: %d", devicePubKey.size());
        return std::make_tuple(issuerIdentifier, DOES_NOT_EXIST);
      }
      devicePubKey.insert(devicePubKey.begin(), 0x04);
      std::vector<uint8_t> endpointId = getHashIdentifier(devicePubKey, false);
      if (endpointId.size() < 6) {
        LOG(E, "Device credential request produced invalid endpoint hash");
        return std::make_tuple(issuerIdentifier, DOES_NOT_EXIST);
      }
      for (auto& endpoint : foundIssuer->endpoints) {
        if (endpoint.endpoint_id.size() <= endpointId.size() &&
            std::equal(endpoint.endpoint_id.begin(), endpoint.endpoint_id.end(), endpointId.begin())) {
          LOG(I, "Found existing endpoint with identifier length %zu", endpoint.endpoint_id.size());
          foundEndpoint = &endpoint;
        }
      }
      if (foundEndpoint == 0) {
        LOG(I, "Adding new endpoint with hash length %zu and public-key length %zu", endpointId.size(), devicePubKey.size());
        hkEndpoint_t endpoint;
        std::vector<uint8_t> x_coordinate = get_x(devicePubKey);
        if (x_coordinate.empty()) {
          LOG(E, "Device credential request produced an invalid X coordinate");
          return std::make_tuple(issuerIdentifier, DOES_NOT_EXIST);
        }
        tlv_it tlvKeyType = dcrTlv.find(kDevice_Req_Key_Type);
        if (tlvKeyType == dcrTlv.end() || tlvKeyType->length() == 0 || tlvKeyType->data() == nullptr) {
          LOG(E, "Device credential request missing key type");
          return std::make_tuple(issuerIdentifier, DOES_NOT_EXIST);
        }
        if (tlvKeyType->value.empty()) {
          LOG(E, "Device credential request key type is empty");
          return std::make_tuple(issuerIdentifier, DOES_NOT_EXIST);
        }
        endpoint.counter = 0;
        endpoint.key_type = tlvKeyType->value.front();
        endpoint.last_used_at = 0;
        // endpoint.enrollments.hap = hap;
        endpoint.endpoint_id.assign(endpointId.begin(), endpointId.begin() + 6);
        endpoint.endpoint_pk = devicePubKey;
        endpoint.endpoint_pk_x = x_coordinate;
        foundIssuer->endpoints.emplace_back(endpoint);
        save_cb(readerData);
        return std::make_tuple(foundIssuer->issuer_id, SUCCESS);
      }
      else {
        LOG(I, "Endpoint already exists");
        save_cb(readerData);
        return std::make_tuple(issuerIdentifier, DUPLICATE);
      }
    }
    else {
      LOG(I, "Issuer does not exist");
      save_cb(readerData);
      return std::make_tuple(issuerIdentifier, DOES_NOT_EXIST);
    }
  }
  return std::make_tuple(readerData.reader_gid, DOES_NOT_EXIST);
}

int HK_HomeKit::set_reader_key(std::vector<uint8_t>& buf) {
  // removed fmt log
  TLV8 rkrTLv;
  rkrTLv.parse(buf.data(), buf.size());
  tlv_it tlvReaderKey = rkrTLv.find(kReader_Req_Reader_Private_Key);
  if(tlvReaderKey == rkrTLv.end()){ LOG(D, "kReader_Req_Reader_Private_Key not found"); return -1;}
  std::vector<uint8_t> readerKey = tlvReaderKey->value;
  tlv_it tlvUniqueId = rkrTLv.find(kReader_Req_Identifier);
  if(tlvUniqueId == rkrTLv.end()){ LOG(D, "kReader_Req_Identifier not found"); return -1;}
  std::vector<uint8_t> uniqueIdentifier = tlvUniqueId->value;
  if (readerKey.size() > 0 && uniqueIdentifier.size() > 0) {
    // removed fmt log
    // removed fmt log
    std::vector<uint8_t> pubKey = getPublicKey(readerKey.data(), readerKey.size());
    // removed fmt log
    std::vector<uint8_t> x_coordinate = get_x(pubKey);
    // removed fmt log
    readerData.reader_pk_x = x_coordinate;
    readerData.reader_pk = pubKey;
    readerData.reader_sk = readerKey;
    readerData.reader_id = uniqueIdentifier;
    std::vector<uint8_t> readeridentifier = getHashIdentifier(readerData.reader_sk, true);
    // removed fmt log
    readerData.reader_gid = std::vector<uint8_t>{readeridentifier.begin(), readeridentifier.begin() + 8};
    save_cb(readerData);
  }
  return 0;
}
