# PN532 C++ Driver

A cross-platform PN532 NFC/RFID driver library written in modern C++. This project provides a hardware abstraction layer (HAL) approach, allowing the PN532 driver to work across different platforms through platform-specific HAL implementations.

- **Core Driver**: Platform-independent PN532 protocol implementation
- **HAL Implementations**: Platform-specific communication layers (I2C, SPI, UART)

This modular design allows the same driver logic to work on any platform by simply implementing the required HAL interface.

## Platforms

Currently supported platforms:

- **ESP32/ESP-IDF** via [@rednblkx/esp-hal-pn532](https://github.com/rednblkx/esp-hal-pn532)

Additional platform implementations are welcome!  See [Contributing](#contributing) for details.

## Features

-  **Hardware Abstraction Layer** - Clean interface for platform-specific implementations
-  **NFC Support** - Read and write NFC tags and cards
-  **Multiple Interfaces** - Support for I2C, SPI, and UART communication
-  **Modern C++** - Clean, efficient, and maintainable codebase
-  **Cross-Platform** - Easy to port to new platforms

## Getting Started

### Prerequisites

- C++20 or newer
- PN532 module
- Platform-specific HAL implementation

## Architecture

```
┌─────────────────────────────────────┐
│   Application Code                  │
├─────────────────────────────────────┤
│   PN532 Core Driver (pn532_cxx)    │
├─────────────────────────────────────┤
│   Platform HAL Layer                │
│   (I2C/SPI/UART Interface)          │
├─────────────────────────────────────┤
│   Hardware (PN532 Device)           │
└─────────────────────────────────────┘
```

## Implementing Your Own HAL

To port this driver to a new platform, implement the required HAL interface and pass it to the `Frontend` constructor:

```cpp
class YourPlatformTransportHAL : Transport {
public: 
    // Initialize the interface
    bool YourPlatformTransportHAL();
    
    void swReset() override;
    void abort() override;
    Transaction begin() override;

private:
    Status writeChunk(std::span<const uint8_t> data) override;
    bool waitReady(uint32_t timeout_ms) override;
    Status prepareRead() override;
    Status readChunk(std::span<uint8_t> buffer) override;
    void endTransaction() override;
};
```

See the [esp-hal-pn532](https://github.com/rednblkx/esp-hal-pn532) repository for a complete reference implementation.

## License

This project is licensed under the MIT License.  Please see the LICENSE file for details.

## Contributing

Contributions are welcome!  Areas of interest: 

- New platform HAL implementations (STM32, Arduino, etc.)
- Additional PN532 features and commands
- Bug fixes and improvements
- Documentation and examples

Feel free to open an issue or pull request. 

## Resources

- [PN532/C1 Datasheet](https://www.nxp.com/docs/en/nxp/data-sheets/PN532_C1.pdf)
- [PN532 User Manual](https://www.nxp.com/docs/en/user-guide/141520.pdf)

## Related Projects

- [esp-hal-pn532](https://github.com/rednblkx/esp-hal-pn532) - ESP32/ESP-IDF HAL implementation
