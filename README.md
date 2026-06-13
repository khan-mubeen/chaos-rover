# Chaos Rover

A wireless interactive rover built at UBISS 2026 Workshop C: Robot Makers, University of Oulu.

**Team:** Mubeen Khan & Eshmam Rayed

## What it does
A two-mode rover controlled wirelessly via ESP-NOW between two ESP32 boards.
Normal Mode behaves predictably. Chaos Mode reshuffles the joystick mapping every 20 seconds.

## Hardware
- ESP32-WROVER (rover) and ESP32 standard (controller)
- 2 continuous rotation servos
- OLED face display, 16x2 LCD, 32-LED strip, buzzer
- LED bar graphs, joystick, toggle switch, push buttons

## Code
- `controller/` - ESP32 controller code
- `rover/` - ESP32-WROVER rover code
