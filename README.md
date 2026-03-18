# CO2 Monitor

This is an air quality monitoring project built around an air quality station built with an ESP32. The air quality station sends data to a Go API running on my VPS.

## Architecture

`ESP32 -> Wi-Fi -> HTTPS -> nginx -> Go API -> SQLite`

- The ESP32 sensors take readings every 5 seconds, and upload every 60 seconds
- device uploads JSON to the API
- Go service stores the reading
- graphing frontend gets data from the API and draws the charts

## Hardware

- ESP32
- SCD41 sensor (CO2, temperature, humidity)
- BMP280 sensor (air pressure)
- SSD1306 OLED display
- Breadboard

## Backend

This repo is split into `/api` and `/single-shot`.
The Go API lives in `/api`.

Responsibilities:
- `/records` endpoints: `GET` and `POST`
- `/healthz` endpoint
- handle reading uploads
- check API key on the `POST` endpoint
- store records in SQLite
- expose the graphing page

## Deployment steps for when I forget

The Go service runs on an Ubuntu VPS behind `nginx` and `systemd`.

See `/scripts/release.sh`. Run it in WSL. You'll need the VPS SSH key passphrase

## Obstacles

A few problems I had to overcome:
- learning how to solder (took a few attempts)
- building and testing a Go binary
- deploying a Go binary to 
- learning what Arduino sketches are
- trying to get the ESP32 to talk to my WiFi -- I had to switch my router to dual-band
- configuring `systemd`, `nginx`, HTTPS, and SQLite permissions
- don't commit your API secrets to git 🤢

## Running Locally

### Backend

From WSL: `./run-locally.sh`

OR

```bash
go test ./...
go run ./api
```

Hit `http://127.0.0.1:8080/`