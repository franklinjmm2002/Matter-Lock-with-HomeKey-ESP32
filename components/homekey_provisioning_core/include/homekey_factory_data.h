#pragma once

#include <cstdint>
#include <vector>

bool HomeKeyFactoryDataReadIssuerPublicKey(std::vector<uint8_t> & public_key);
bool HomeKeyFactoryDataWriteIssuerPublicKey(const std::vector<uint8_t> & public_key);
bool HomeKeyFactoryDataClearIssuerPublicKey();
bool HomeKeySeedIssuerFromFactoryDataOrConfig(const char * config_hex);
