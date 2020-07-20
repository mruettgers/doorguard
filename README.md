# Doorguard

A secure (MIFARE DESFire based) NFC access control system for the ESP8266.

## Motivation

This project was brought to life because I was looking for an inexpensive solution to control the electronic door lock in our new building in a secure way. And because I have never been a fan of fingerprint scanners for access control and also given the price of such systems I was looking for an NFC based alternative.

But the most important factor that made me developing something of my own was the fact that in 2019 there were still access control products on the market whose only security feature was to verify the ID of a passive 125 KHz NFC tag. No authentication, no encryption, just an ID that can be copied easily. WTF?

The door station I'm talking about, and which I unfortunately had already flush-mounted, is the Doorbird D2101V. At first I was still hoping that some kind of a proprietary protocol would be used to make the communication between the NFC reader more secure.

But then I had to realize that the NFC tag cloner I bought for about 10 € on Ebay allowed me to make copies of the original Doorbird tags that could be successfully used to open the door.  
Sorry Doorbird, but you can do better.

After some research it turned out that modern, secure NFC-based access systems operate at 13,56 MHz and are often used together with Mifare DESfire tags, which offer both authentication and encryption between the tag and the NFC reader and that are considered as state of the art.

Finding software with support for NFC and Mifare DESfire for the ESP8266 proved to be extremely difficult. But luckily I found the great project [DIY electronic RFID Door Lock with Battery Backup (2016)](https://www.codeproject.com/Articles/1096861/DIY-electronic-RFID-Door-Lock-with-Battery-Backup) by Elmü, which I started to port to the ESP8266.  
That's what this is all about.

## Hardware

TBD.

## Software

TBD.
