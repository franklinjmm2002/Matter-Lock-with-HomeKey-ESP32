/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_matter_console.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>

#include "homekey_factory_data.h"
#include "homekey_reader_data_manager.h"
#include "homekey_provisioning_manager.h"

namespace {
constexpr const char *TAG = "homekey_console";
esp_matter::console::engine g_homekey_console;

bool parse_hex(const char * input, std::vector<uint8_t> & out)
{
    if (input == nullptr) {
        return false;
    }

    std::string hex(input);
    hex.erase(std::remove_if(hex.begin(), hex.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), hex.end());
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) {
        hex.erase(0, 2);
    }
    if (hex.empty() || (hex.size() % 2) != 0) {
        return false;
    }

    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const auto hi = static_cast<unsigned char>(hex[i]);
        const auto lo = static_cast<unsigned char>(hex[i + 1]);
        if (!std::isxdigit(hi) || !std::isxdigit(lo)) {
            return false;
        }
        out.push_back(static_cast<uint8_t>(strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return true;
}

bool parse_u8(const char * input, uint8_t & out)
{
    if (input == nullptr || *input == '\0') {
        return false;
    }

    char * end = nullptr;
    const unsigned long value = strtoul(input, &end, 0);
    if (end == nullptr || *end != '\0' || value > std::numeric_limits<uint8_t>::max()) {
        return false;
    }

    out = static_cast<uint8_t>(value);
    return true;
}

std::string to_hex(const std::vector<uint8_t> & data)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        out.push_back(kHex[(byte >> 4) & 0x0F]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

bool derive_p256_public_key(const std::vector<uint8_t> & private_key, std::vector<uint8_t> & public_key)
{
    auto rng_cb = [](void *, unsigned char * buf, size_t len) -> int {
        esp_fill_random(buf, len);
        return 0;
    };

    mbedtls_ecp_keypair keypair;
    mbedtls_ecp_keypair_init(&keypair);

    const int read_res =
        mbedtls_ecp_read_key(MBEDTLS_ECP_DP_SECP256R1, &keypair, private_key.data(), private_key.size());
    if (read_res != 0) {
        ESP_LOGE(TAG, "Failed to parse reader private key: %d", read_res);
        mbedtls_ecp_keypair_free(&keypair);
        return false;
    }

    const int mul_res =
        mbedtls_ecp_mul(&keypair.private_grp, &keypair.private_Q, &keypair.private_d, &keypair.private_grp.G, rng_cb, nullptr);
    if (mul_res != 0) {
        ESP_LOGE(TAG, "Failed to derive reader public key: %d", mul_res);
        mbedtls_ecp_keypair_free(&keypair);
        return false;
    }

    size_t olen = 0;
    public_key.resize(MBEDTLS_ECP_MAX_PT_LEN);
    const int write_res = mbedtls_ecp_point_write_binary(&keypair.private_grp, &keypair.private_Q,
                                                         MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, public_key.data(),
                                                         public_key.size());
    mbedtls_ecp_keypair_free(&keypair);
    if (write_res != 0) {
        ESP_LOGE(TAG, "Failed to encode reader public key: %d", write_res);
        return false;
    }

    public_key.resize(olen);
    return true;
}

std::vector<uint8_t> extract_x_coordinate(const std::vector<uint8_t> & public_key)
{
    std::vector<uint8_t> mutable_key = public_key;
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);

    std::vector<uint8_t> x;
    if (mbedtls_ecp_point_read_binary(&group, &point, mutable_key.data(), mutable_key.size()) == 0) {
        const size_t x_size = mbedtls_mpi_size(&point.private_X);
        x.resize(x_size);
        if (mbedtls_mpi_write_binary(&point.private_X, x.data(), x_size) != 0) {
            x.clear();
        }
    }

    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return x;
}

std::vector<uint8_t> key_identifier(const std::vector<uint8_t> & key, size_t output_len)
{
    static constexpr char prefix[] = "key-identifier";
    std::vector<uint8_t> material(prefix, prefix + strlen(prefix));
    material.insert(material.end(), key.begin(), key.end());

    uint8_t hash[32] = {};
    mbedtls_sha256(material.data(), material.size(), hash, 0);
    return std::vector<uint8_t>(hash, hash + output_len);
}

esp_err_t homekey_status_handler(int argc, char ** argv)
{
    (void) argc;
    (void) argv;
    if (!HomeKeyReaderDataMgr().Begin()) {
        printf("reader data unavailable\n");
        return ESP_FAIL;
    }

    const readerData_t reader_data = HomeKeyReaderDataMgr().GetReaderDataCopy();
    printf("provisioned: %s\n", HomeKeyReaderDataMgr().IsProvisioned() ? "yes" : "no");
    printf("reader_gid: %s\n", to_hex(reader_data.reader_gid).c_str());
    printf("reader_id: %s\n", to_hex(reader_data.reader_id).c_str());
    printf("issuers: %u\n", static_cast<unsigned>(reader_data.issuers.size()));
    return ESP_OK;
}

esp_err_t homekey_dump_handler(int argc, char ** argv)
{
    (void) argc;
    (void) argv;
    if (!HomeKeyReaderDataMgr().Begin()) {
        printf("reader data unavailable\n");
        return ESP_FAIL;
    }

    const readerData_t reader_data = HomeKeyReaderDataMgr().GetReaderDataCopy();
    printf("reader_gid=%s\n", to_hex(reader_data.reader_gid).c_str());
    printf("reader_id=%s\n", to_hex(reader_data.reader_id).c_str());
    printf("reader_pk=%s\n", to_hex(reader_data.reader_pk).c_str());
    printf("reader_pk_x=%s\n", to_hex(reader_data.reader_pk_x).c_str());
    printf("issuers=%u\n", static_cast<unsigned>(reader_data.issuers.size()));
    for (size_t i = 0; i < reader_data.issuers.size(); ++i) {
        const auto & issuer = reader_data.issuers[i];
        printf("issuer[%u].id=%s\n", static_cast<unsigned>(i), to_hex(issuer.issuer_id).c_str());
        printf("issuer[%u].pk=%s\n", static_cast<unsigned>(i), to_hex(issuer.issuer_pk).c_str());
        printf("issuer[%u].endpoints=%u\n", static_cast<unsigned>(i), static_cast<unsigned>(issuer.endpoints.size()));
    }
    return ESP_OK;
}

esp_err_t homekey_set_reader_handler(int argc, char ** argv)
{
    if (argc != 2) {
        printf("usage: set-reader <reader_private_key_hex> <reader_identifier_hex>\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (!HomeKeyReaderDataMgr().Begin()) {
        printf("reader data unavailable\n");
        return ESP_FAIL;
    }

    std::vector<uint8_t> reader_sk;
    std::vector<uint8_t> reader_id;
    if (!parse_hex(argv[0], reader_sk) || !parse_hex(argv[1], reader_id)) {
        printf("invalid hex input\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> reader_pk;
    if (!derive_p256_public_key(reader_sk, reader_pk)) {
        printf("failed to derive reader public key\n");
        return ESP_FAIL;
    }

    readerData_t reader_data = HomeKeyReaderDataMgr().GetReaderDataCopy();
    reader_data.reader_sk = reader_sk;
    reader_data.reader_id = reader_id;
    reader_data.reader_pk = reader_pk;
    reader_data.reader_pk_x = extract_x_coordinate(reader_pk);
    reader_data.reader_gid = key_identifier(reader_sk, 8);

    if (HomeKeyReaderDataMgr().UpdateReaderData(reader_data) == nullptr) {
        printf("failed to save reader data\n");
        return ESP_FAIL;
    }

    printf("reader configured\n");
    printf("reader_gid=%s\n", to_hex(reader_data.reader_gid).c_str());
    printf("reader_pk=%s\n", to_hex(reader_data.reader_pk).c_str());
    return ESP_OK;
}

esp_err_t homekey_add_issuer_handler(int argc, char ** argv)
{
    if (argc != 1) {
        printf("usage: add-issuer <issuer_ed25519_public_key_hex>\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (!HomeKeyReaderDataMgr().Begin()) {
        printf("reader data unavailable\n");
        return ESP_FAIL;
    }

    std::vector<uint8_t> issuer_pk;
    if (!parse_hex(argv[0], issuer_pk) || issuer_pk.size() != 32) {
        printf("issuer public key must be 32 bytes of hex\n");
        return ESP_ERR_INVALID_ARG;
    }

    const std::vector<uint8_t> issuer_id = key_identifier(issuer_pk, 8);
    if (!HomeKeyReaderDataMgr().AddIssuerIfNotExists(issuer_id, issuer_pk.data())) {
        printf("issuer already exists or could not be added\n");
        return ESP_FAIL;
    }

    printf("issuer added\n");
    printf("issuer_id=%s\n", to_hex(issuer_id).c_str());
    return ESP_OK;
}

esp_err_t homekey_process_tlv_handler(int argc, char ** argv)
{
    if (argc != 1) {
        printf("usage: process-tlv <homekey_tlv_hex>\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> request;
    if (!parse_hex(argv[0], request)) {
        printf("invalid hex input\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> response;
    if (!HomeKeyProvisioningMgr().ProcessRequest(request, response)) {
        printf("failed to process Home Key provisioning request\n");
        return ESP_FAIL;
    }

    printf("request_len=%u\n", static_cast<unsigned>(request.size()));
    printf("response_len=%u\n", static_cast<unsigned>(response.size()));
    printf("response=%s\n", to_hex(response).c_str());
    return ESP_OK;
}

esp_err_t homekey_reader_read_tlv_handler(int argc, char ** argv)
{
    (void) argc;
    (void) argv;

    std::vector<uint8_t> request;
    if (!HomeKeyProvisioningMgr().BuildReaderKeyReadRequest(request)) {
        printf("failed to build reader read request\n");
        return ESP_FAIL;
    }

    std::vector<uint8_t> response;
    if (!HomeKeyProvisioningMgr().ProcessRequest(request, response)) {
        printf("failed to process reader read request\n");
        return ESP_FAIL;
    }

    printf("request=%s\n", to_hex(request).c_str());
    printf("response=%s\n", to_hex(response).c_str());
    return ESP_OK;
}

esp_err_t homekey_reader_write_tlv_handler(int argc, char ** argv)
{
    if (argc != 2) {
        printf("usage: set-reader-tlv <reader_private_key_hex> <reader_identifier_hex>\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> reader_sk;
    std::vector<uint8_t> reader_id;
    if (!parse_hex(argv[0], reader_sk) || !parse_hex(argv[1], reader_id)) {
        printf("invalid hex input\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> request;
    if (!HomeKeyProvisioningMgr().BuildReaderKeyWriteRequest(reader_sk, reader_id, request)) {
        printf("failed to build reader write request\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> response;
    if (!HomeKeyProvisioningMgr().ProcessRequest(request, response)) {
        printf("failed to process reader write request\n");
        return ESP_FAIL;
    }

    printf("request=%s\n", to_hex(request).c_str());
    printf("response=%s\n", to_hex(response).c_str());
    return ESP_OK;
}

esp_err_t homekey_add_endpoint_handler(int argc, char ** argv)
{
    if (argc != 3) {
        printf("usage: add-endpoint <issuer_id_hex> <endpoint_public_key_hex> <key_type>\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> issuer_id;
    std::vector<uint8_t> endpoint_public_key;
    uint8_t key_type = 0;
    if (!parse_hex(argv[0], issuer_id) || !parse_hex(argv[1], endpoint_public_key) || !parse_u8(argv[2], key_type)) {
        printf("invalid issuer, endpoint key, or key_type\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> request;
    if (!HomeKeyProvisioningMgr().BuildDeviceCredentialWriteRequest(issuer_id, endpoint_public_key, key_type, request)) {
        printf("failed to build device credential request\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> response;
    if (!HomeKeyProvisioningMgr().ProcessRequest(request, response)) {
        printf("failed to process device credential request\n");
        return ESP_FAIL;
    }

    printf("request=%s\n", to_hex(request).c_str());
    printf("response=%s\n", to_hex(response).c_str());
    return ESP_OK;
}

esp_err_t homekey_factory_issuer_handler(int argc, char ** argv)
{
    (void) argc;
    (void) argv;

    std::vector<uint8_t> issuer_pk;
    if (!HomeKeyFactoryDataReadIssuerPublicKey(issuer_pk)) {
        printf("factory_issuer=unset\n");
        return ESP_OK;
    }

    printf("factory_issuer=%s\n", to_hex(issuer_pk).c_str());
    return ESP_OK;
}

esp_err_t homekey_set_factory_issuer_handler(int argc, char ** argv)
{
    if (argc != 1) {
        printf("usage: set-factory-issuer <issuer_ed25519_public_key_hex>\n");
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> issuer_pk;
    if (!parse_hex(argv[0], issuer_pk) || issuer_pk.size() != 32) {
        printf("issuer public key must be 32 bytes of hex\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (!HomeKeyFactoryDataWriteIssuerPublicKey(issuer_pk)) {
        printf("failed to store factory issuer public key\n");
        return ESP_FAIL;
    }

    printf("factory issuer stored\n");
    printf("factory_issuer=%s\n", to_hex(issuer_pk).c_str());
    return ESP_OK;
}

esp_err_t homekey_clear_factory_issuer_handler(int argc, char ** argv)
{
    (void) argc;
    (void) argv;

    if (!HomeKeyFactoryDataClearIssuerPublicKey()) {
        printf("failed to clear factory issuer public key\n");
        return ESP_FAIL;
    }

    printf("factory issuer cleared\n");
    return ESP_OK;
}

esp_err_t homekey_erase_handler(int argc, char ** argv)
{
    (void) argc;
    (void) argv;
    if (!HomeKeyReaderDataMgr().Begin()) {
        printf("reader data unavailable\n");
        return ESP_FAIL;
    }

    if (!HomeKeyReaderDataMgr().DeleteAllReaderData()) {
        printf("failed to erase reader data\n");
        return ESP_FAIL;
    }
    printf("reader data erased\n");
    return ESP_OK;
}

esp_err_t homekey_dispatch(int argc, char ** argv)
{
    if (argc <= 0) {
        g_homekey_console.for_each_command(esp_matter::console::print_description, nullptr);
        return ESP_OK;
    }
    return g_homekey_console.exec_command(argc, argv);
}
} // namespace

esp_err_t homekey_console_register_commands()
{
    static const esp_matter::console::command_t command = {
        .name = "homekey",
        .description = "Home Key provisioning commands. Usage matter esp homekey <command>.",
        .handler = homekey_dispatch,
    };

    static const esp_matter::console::command_t homekey_commands[] = {
        {
            .name = "status",
            .description = "Show provisioning status.",
            .handler = homekey_status_handler,
        },
        {
            .name = "dump",
            .description = "Dump stored reader and issuer data.",
            .handler = homekey_dump_handler,
        },
        {
            .name = "set-reader",
            .description = "Set reader private key and identifier.",
            .handler = homekey_set_reader_handler,
        },
        {
            .name = "add-issuer",
            .description = "Add an issuer from its 32-byte Ed25519 public key.",
            .handler = homekey_add_issuer_handler,
        },
        {
            .name = "set-reader-tlv",
            .description = "Provision the reader through the Home Key TLV flow.",
            .handler = homekey_reader_write_tlv_handler,
        },
        {
            .name = "read-reader-tlv",
            .description = "Fetch the current reader key response through the Home Key TLV flow.",
            .handler = homekey_reader_read_tlv_handler,
        },
        {
            .name = "add-endpoint",
            .description = "Provision an endpoint for an issuer using a Home Key device credential TLV.",
            .handler = homekey_add_endpoint_handler,
        },
        {
            .name = "process-tlv",
            .description = "Process a raw Home Key TLV provisioning request.",
            .handler = homekey_process_tlv_handler,
        },
        {
            .name = "factory-issuer",
            .description = "Show the stored factory issuer public key.",
            .handler = homekey_factory_issuer_handler,
        },
        {
            .name = "set-factory-issuer",
            .description = "Store the factory issuer 32-byte Ed25519 public key.",
            .handler = homekey_set_factory_issuer_handler,
        },
        {
            .name = "clear-factory-issuer",
            .description = "Clear the stored factory issuer public key.",
            .handler = homekey_clear_factory_issuer_handler,
        },
        {
            .name = "erase",
            .description = "Erase all stored Home Key reader data.",
            .handler = homekey_erase_handler,
        },
    };

    g_homekey_console.register_commands(homekey_commands, sizeof(homekey_commands) / sizeof(homekey_commands[0]));
    return esp_matter::console::add_commands(&command, 1);
}
