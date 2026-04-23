#!/usr/bin/env python3
import argparse
import sqlite3
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Read gateway sqlite database")
    parser.add_argument(
        "--db",
        default=str(Path(__file__).resolve().parent.parent / "messages.db"),
        help="Path to sqlite db file (default: <project-root>/messages.db)",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=20,
        help="How many newest rows to print from messages table",
    )
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database file not found: {db_path}")
        print("Tip: use --db with [persistence].db_path from gateway.ini")
        return 1

    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    tables = [row[0] for row in cur.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
    print(f"DB: {db_path}")
    print("Tables:", ", ".join(tables) if tables else "(none)")

    if "messages" not in tables:
        print("messages table not found")
        conn.close()
        return 0

    count = cur.execute("SELECT COUNT(*) FROM messages").fetchone()[0]
    print(f"messages count: {count}")

    query = (
        "SELECT id, topic, payload_len, qos, status, retry_count, create_time, update_time "
        "FROM messages ORDER BY id DESC LIMIT ?"
    )
    rows = cur.execute(query, (max(args.limit, 1),)).fetchall()

    if not rows:
        print("messages table is empty")
        conn.close()
        return 0

    print("latest messages:")
    for row in rows:
        print(
            f"id={row['id']} topic={row['topic']} payload_len={row['payload_len']} "
            f"qos={row['qos']} status={row['status']} retry={row['retry_count']} "
            f"create={row['create_time']} update={row['update_time']}"
        )

    conn.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
