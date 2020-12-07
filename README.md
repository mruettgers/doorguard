# Doorguard

A secure (MIFARE DESFire based) RFID access control system for the ESP8266.

## Motivation

This project was brought to life because I was looking for an inexpensive solution to control the electronic door lock in our new building in a secure way. And because I have never been a fan of fingerprint scanners for access control and also given the price of such systems I was looking for an RFID based alternative.

But the most important factor that made me developing something of my own was the fact that in 2019 there were still access control products on the market whose only security feature was to verify the ID of a passive 125 KHz RFID tag. No authentication, no encryption, just an ID that can be copied easily. WTF?

The door station I'm talking about, and which I unfortunately had already flush-mounted, is the Doorbird D2101V. At first I was still hoping that some kind of a proprietary protocol would be used to make the communication between the RFID reader more secure.

But then I had to realize that the RFID tag cloner I bought for about 10 € on Ebay allowed me to make copies of the original Doorbird tags that could be successfully used to open the door.  
Sorry Doorbird, but you can do better.

After some research it turned out that modern, secure RFID-based access systems operate at 13,56 MHz and are often used together with Mifare DESfire tags, which offer both authentication and encryption between the tag and the RFID reader and that are considered as state of the art.

Finding software with support for RFID and Mifare DESfire for the ESP8266 proved to be extremely difficult. But luckily I found the great project [DIY electronic RFID Door Lock with Battery Backup (2016)](https://www.codeproject.com/Articles/1096861/DIY-electronic-RFID-Door-Lock-with-Battery-Backup) by Elmü, which I started to port to the ESP8266.  
Thanks to [step21](https://github.com/step21) the main work has already been done with porting the [library](https://github.com/step21/desfire_rfid) for Arduino.  
That's what this is all about.

## Hardware

### Wemos D1 Mini

At the heart of the project is the microcontroller based on ESP8266. As in most of my projects, the Wemos D1 Mini is used here as well.

### NFC-Shield

I have tested some NFC shields in advance to make sure that the reader or any existing external antenna can be hidden behind the plaster without limiting the functionality.

The [NFC-Shield 2.0 from Seeed](https://wiki.seeedstudio.com/NFC_Shield_V2.0/), which is also equipped with an external antenna, worked best. The small dimensions of the antenna made it easy to place it in a small, hollowed out gap behind the plaster.

And fortunately, the circuit board of the NFC shield was build in such a way that the form factor could be minimized with the help of a Dremel so that it could easily be placed inside the case of the Doorbird.

TBD.

## Software

TBD.

## Credits
* [DIY electronic RFID Door Lock with Battery Backup (2016)](https://www.codeproject.com/Articles/1096861/DIY-electronic-RFID-Door-Lock-with-Battery-Backup) (The original project by Elmü on which this project is based on) 
* [https://github.com/step21/desfire_rfid](https://github.com/step21/desfire_rfid) (Arduino port of the RFID library with DESFire support)

## Roadmap

Unordered list of features that should make it into the v1.0

* [ ] Documentation
  * [ ] Hardware
  * [ ] Software
* [ ] Code refactoring
* [ ] MQTT Support
* [ ] REST-API

## License

Distributed under the GPL v3 license.  
See [LICENSE](LICENSE) for more information.

