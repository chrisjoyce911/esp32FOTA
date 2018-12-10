esp32FOTA library for Arduino
================================

# Purpose

A simple library to add support for Over-The-Air (OTA) updates to your project.

## Features
- [x] Web update (requires web server)
- [x] Batch firmware sync 
- [ ] Stream update (e.g. MQTT or other)

## How it works

This library tries to connect to configured APs first. Then if those fail ,tries to scan for open networks and connect to those. If it is not possible to connect to neither configured nor open networks - it falls back to AP mode.