#!/usr/bin/env python3
import argparse
import json
import sys

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("missing dependency: paho-mqtt (pip install paho-mqtt)", file=sys.stderr)
    sys.exit(1)


def on_connect(client, userdata, flags, rc):
    topic = userdata["topic"]
    if rc != 0:
        print(f"[ERR] connect failed rc={rc}", file=sys.stderr)
        return
    client.subscribe(topic, qos=userdata["qos"])
    print(f"[OK] subscribed topic={topic} qos={userdata['qos']}")


def on_message(client, userdata, msg):
    payload_bytes = msg.payload
    try:
        obj = json.loads(payload_bytes.decode("utf-8", errors="replace"))
    except Exception as exc:
        print(f"[ERR] invalid json topic={msg.topic}: {exc}")
        print(payload_bytes.decode("utf-8", errors="replace"))
        return

    interface = obj.get("interface", "")
    device_path = obj.get("device_path", "")
    protocol_family = obj.get("protocol_family", "")
    protocol_name = obj.get("protocol_name", "")
    payload = obj.get("payload", {})
    conn_type = payload.get("connection_type", "")
    dev_id = payload.get("id", "")
    data_hex = payload.get("data", "")
    print("----- MQTT Envelope -----")
    print(f"topic={msg.topic}")
    print(f"interface={interface}")
    print(f"device_path={device_path}")
    print(f"protocol_family={protocol_family}")
    print(f"protocol_name={protocol_name}")
    print(f"payload.connection_type={conn_type}")
    print(f"payload.id={dev_id}")
    print(f"payload.data={data_hex}")
    print("-------------------------")


def main():
    parser = argparse.ArgumentParser(description="MQTT envelope decoder for gateway uplink")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic", default="gateway/data")
    parser.add_argument("--qos", type=int, default=1)
    parser.add_argument("--client-id", default="gateway-mqtt-envelope-sub")
    args = parser.parse_args()

    userdata = {"topic": args.topic, "qos": args.qos}
    client = mqtt.Client(client_id=args.client_id, userdata=userdata)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.host, args.port, keepalive=60)
    print(f"[INFO] connecting mqtt://{args.host}:{args.port} topic={args.topic}")
    client.loop_forever()


if __name__ == "__main__":
    main()
