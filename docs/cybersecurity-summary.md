(1) MQTT Authentication — Mosquitto uses a password file, anonymous connections are disabled;
(2) Topic ACLs — explain the acl.conf rules, which user can publish/subscribe to what;
(3) Secrets Management — .env file holds credentials, .gitignore prevents it being committed to GitHub;
(4) LWT Reliability — explain that the ESP32 pre-registers a 'OFFLINE' message with the broker, which fires automatically if the ESP32 disconnects unexpectedly.
