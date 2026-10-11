"""Live measurement charts from the ESP8266 TCP server using the VERI command."""

import argparse
import ipaddress
import math
import queue
import random
import re
import socket
import sqlite3
import threading
from collections import deque
from datetime import datetime
from pathlib import Path

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

PORT = 8080
INTERVAL = 2.0
LED_THRESHOLD = 25.0
DATABASE_PATH = Path(__file__).resolve().with_name("EnviroGuard.db")
PATTERN = re.compile(
    r"Temperature: (-?\d+(?:\.\d+)?) C \| Humidity: (\d+(?:\.\d+)?) %"
)


def parse_measurement(line):
    match = PATTERN.fullmatch(line.strip())
    if not match:
        return None
    temperature, humidity = map(float, match.groups())
    if -40 <= temperature <= 80 and 0 <= humidity <= 100:
        return temperature, humidity
    return None


def read_reply(connection, pending):
    while b"\n" not in pending:
        chunk = connection.recv(1024)
        if not chunk:
            raise ConnectionError("ESP closed the connection")
        pending += chunk
        if len(pending) > 4096:
            raise ConnectionError("Unexpectedly long response")
    line, pending = pending.split(b"\n", 1)
    return line.decode("ascii", errors="replace").strip(), pending


def collect(ip, events, stop):
    database = None
    try:
        database = sqlite3.connect(DATABASE_PATH, timeout=5)
        database.execute("""
            CREATE TABLE IF NOT EXISTS measurements (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                recorded_at TEXT NOT NULL,
                temperature_c REAL NOT NULL CHECK(temperature_c BETWEEN -40 AND 80),
                humidity_percent REAL NOT NULL CHECK(humidity_percent BETWEEN 0 AND 100),
                warning_state TEXT NOT NULL CHECK(warning_state IN ('NORMAL', 'HIGH_TEMPERATURE')),
                source_ip TEXT NOT NULL
            )
        """)
        database.commit()
        print(f"SQLite database: {DATABASE_PATH}")
        collect_stream(ip, events, stop, database)
    except sqlite3.Error as error:
        message = f"DATABASE ERROR: {error}. Recording stopped."
        print(message)
        events.put(("status", message))
    finally:
        if database is not None:
            database.close()


def collect_stream(ip, events, stop, database):
    while not stop.is_set():
        try:
            events.put(("status", "Connecting to ESP..."))
            with socket.create_connection((ip, PORT), timeout=5) as connection:
                connection.settimeout(5)
                pending = b""
                events.put(("status", "Connected; waiting for measurements..."))
                while not stop.is_set():
                    connection.sendall(b"VERI\n")
                    line, pending = read_reply(connection, pending)
                    values = parse_measurement(line)
                    if values is None:
                        events.put(("status", "ERROR: No current STM32 measurement" if line == "ERROR: Guncel STM32 olcumu yok" else line))
                        events.put(("gap", None))
                    else:
                        stamp = datetime.now().astimezone()
                        temperature, humidity = values
                        warning = "HIGH_TEMPERATURE" if temperature >= LED_THRESHOLD else "NORMAL"
                        database.execute(
                            "INSERT INTO measurements "
                            "(recorded_at, temperature_c, humidity_percent, warning_state, source_ip) "
                            "VALUES (?, ?, ?, ?, ?)",
                            (stamp.isoformat(timespec="milliseconds"), temperature, humidity, warning, ip),
                        )
                        database.commit()
                        events.put(("measurement", (stamp, temperature, humidity)))
                    stop.wait(INTERVAL)
        except (OSError, ConnectionError) as error:
            if stop.is_set():
                break
            events.put(("status", f"Connection unavailable: {error}. Retrying..."))
            events.put(("gap", None))
            stop.wait(3)


def show_history():
    if not DATABASE_PATH.exists():
        print("No database yet. Run the live chart to record measurements first.")
        return
    try:
        with sqlite3.connect(f"{DATABASE_PATH.as_uri()}?mode=ro", uri=True) as database:
            count = database.execute("SELECT COUNT(*) FROM measurements").fetchone()[0]
            rows = database.execute(
                "SELECT recorded_at, temperature_c, humidity_percent, warning_state "
                "FROM measurements ORDER BY id DESC LIMIT 25"
            ).fetchall()
        print(f"Database: {DATABASE_PATH}\nTotal saved measurements: {count}\nLatest 25:")
        for stamp, temperature, humidity, warning in rows:
            print(f"{stamp} | {temperature:.1f} C | {humidity:.1f}% | {warning}")
    except sqlite3.Error as error:
        print(f"DATABASE ERROR: {error}")


class PythonIsolationForest:
    """Small educational Isolation Forest using only Python's standard library.

    Random feature splits isolate rare combinations in shorter paths.
    Scores follow s(x,n) = 2 ** (-mean_path_length / c(n)).
    This implementation is independent of scikit-learn and can give different results.
    """

    @staticmethod
    def expected_path(size):
        if size <= 1:
            return 0.0
        harmonic = sum(1.0 / index for index in range(1, size))
        return 2.0 * harmonic - 2.0 * (size - 1) / size

    def build_tree(self, points, depth):
        if len(points) <= 1 or depth >= self.depth_limit:
            return (len(points),)
        ranges = [(min(point[j] for point in points), max(point[j] for point in points))
                  for j in range(len(points[0]))]
        candidates = [j for j, (low, high) in enumerate(ranges) if low < high]
        if not candidates:
            return (len(points),)
        feature = self.rng.choice(candidates)
        low, high = ranges[feature]
        split = low + self.rng.random() * (high - low)
        if split >= high:
            split = math.nextafter(high, low)
        left = [point for point in points if point[feature] <= split]
        right = [point for point in points if point[feature] > split]
        return (feature, split, self.build_tree(left, depth + 1),
                self.build_tree(right, depth + 1))

    def fit(self, points):
        self.rng = random.Random(42)
        self.sample_size = min(256, len(points))
        if self.sample_size < 2:
            raise ValueError("At least two measurements are required")
        self.depth_limit = math.ceil(math.log2(self.sample_size))
        self.trees = [self.build_tree(self.rng.sample(points, self.sample_size), 0)
                      for _ in range(200)]
        return self

    def path_length(self, point, tree, depth=0):
        if len(tree) == 1:
            return depth + self.expected_path(tree[0])
        feature, split, left, right = tree
        return self.path_length(point, left if point[feature] <= split else right, depth + 1)

    def anomaly_scores(self, points):
        normalizer = self.expected_path(self.sample_size)
        return [2.0 ** (-(sum(self.path_length(point, tree) for tree in self.trees)
                          / len(self.trees)) / normalizer) for point in points]


def show_ai(database_path=DATABASE_PATH):
    """Exploratory outlier detection on recorded data, without changing the database."""
    if not database_path.exists():
        print("No database found. Place EnviroGuard.db beside this Python file.")
        return
    try:
        database = sqlite3.connect(f"{database_path.as_uri()}?mode=ro", uri=True)
        try:
            rows = database.execute(
                "SELECT id, recorded_at, temperature_c, humidity_percent "
                "FROM measurements ORDER BY id"
            ).fetchall()
        finally:
            database.close()
    except sqlite3.Error as error:
        print(f"DATABASE ERROR: {error}")
        return
    if len(rows) < 10:
        print("At least 10 recorded measurements are needed for this demo.")
        return
    try:
        stamps = [datetime.fromisoformat(row[1]) for row in rows]
        features = [[float(row[2]), float(row[3])] for row in rows]
        if not all(math.isfinite(value) for point in features for value in point):
            raise ValueError("Non-finite measurement found")
        if not all(-40 <= point[0] <= 80 and 0 <= point[1] <= 100 for point in features):
            raise ValueError("Measurement outside sensor range")
    except (ValueError, TypeError) as error:
        print(f"DATA ERROR: {error}")
        return

    temperatures = [point[0] for point in features]
    humidities = [point[1] for point in features]
    temp_features = [[value] for value in temperatures]
    hum_features = [[value] for value in humidities]
    temp_scores = PythonIsolationForest().fit(temp_features).anomaly_scores(temp_features)
    hum_scores = PythonIsolationForest().fit(hum_features).anomaly_scores(hum_features)
    temp_flags = [score > 0.5 for score in temp_scores]
    hum_flags = [score > 0.5 for score in hum_scores]
    temp_count, hum_count = sum(temp_flags), sum(hum_flags)
    print(f"Separate Isolation Forest models | Measurements: {len(rows)}")
    print(f"Temperature candidates: {temp_count} | Humidity candidates: {hum_count}")
    print("Exploratory demo: each model is fitted and scored on its own recorded values.")
    print("Outlier candidates are not confirmed faults. Accuracy has not been evaluated.")
    print("Latest 25 results:")
    print("ID | Recorded time | Temperature | Temperature AI | Humidity | Humidity AI")
    for i in range(max(0, len(rows) - 25), len(rows)):
        row = rows[i]
        t_state = "CANDIDATE" if temp_flags[i] else "NOT FLAGGED"
        h_state = "CANDIDATE" if hum_flags[i] else "NOT FLAGGED"
        print(f"{row[0]} | {row[1]} | {temperatures[i]:.1f} C | {t_state} | "
              f"{humidities[i]:.1f}% | {h_state}")

    # Leave visible breaks when recording was interrupted.
    plot_times, plot_temp, plot_humidity = [], [], []
    for index, stamp in enumerate(stamps):
        if index and (stamp - stamps[index - 1]).total_seconds() > 10:
            plot_times.append(stamp)
            plot_temp.append(float("nan"))
            plot_humidity.append(float("nan"))
        plot_times.append(stamp)
        plot_temp.append(temperatures[index])
        plot_humidity.append(humidities[index])

    navy, muted, red = "#172B4D", "#64748B", "#E05252"
    teal, blue = "#0D9488", "#3B82F6"
    figure = plt.figure(figsize=(12, 9), facecolor="#F3F6FA")
    figure.canvas.manager.set_window_title("EnviroGuard - Separate AI Analysis")
    figure.text(0.085, 0.947, "ENVIROGUARD", color=navy, fontsize=23, weight="bold")
    figure.text(0.085, 0.913, "Independent temperature & humidity analysis", color=muted, fontsize=12)
    figure.text(0.915, 0.946, "AI ANALYSIS", color=teal, fontsize=11, weight="bold", ha="right")
    figure.text(0.915, 0.915, "Isolation Forest | Python demo", color=muted, fontsize=10, ha="right")
    summary = [
        (0.085, "RECORDED MEASUREMENTS", str(len(rows)), navy),
        (0.375, "TEMPERATURE CANDIDATES", f"{temp_count} / {len(rows)}", teal),
        (0.665, "HUMIDITY CANDIDATES", f"{hum_count} / {len(rows)}", blue),
    ]
    from matplotlib.patches import FancyBboxPatch
    for x, label, value, color in summary:
        card = FancyBboxPatch((x, 0.799), 0.25, 0.078, boxstyle="round,pad=0.012",
                              transform=figure.transFigure, facecolor="white",
                              edgecolor="#E2E8F0", linewidth=0.8)
        figure.add_artist(card)
        figure.text(x + 0.012, 0.854, label, fontsize=8, color=muted, weight="bold")
        figure.text(x + 0.012, 0.819, value, fontsize=20, color=color, weight="bold")

    temp_ax = figure.add_axes((0.085, 0.467, 0.83, 0.264))
    hum_ax = figure.add_axes((0.085, 0.126, 0.83, 0.264))
    panels = [
        (temp_ax, temperatures, plot_temp, temp_flags, teal, "Temperature", "Temperature (C)", temp_count),
        (hum_ax, humidities, plot_humidity, hum_flags, blue, "Humidity", "Humidity (%)", hum_count),
    ]
    for axis, values, plot_values, flags, color, title, ylabel, count in panels:
        axis.set_facecolor("white")
        axis.plot(plot_times, plot_values, color=color, linewidth=1.8, alpha=0.8)
        axis.scatter([stamp for stamp, flag in zip(stamps, flags) if not flag],
                     [value for value, flag in zip(values, flags) if not flag],
                     color=color, s=19, zorder=3, label="Not flagged")
        axis.scatter([stamp for stamp, flag in zip(stamps, flags) if flag],
                     [value for value, flag in zip(values, flags) if flag],
                     facecolors="white", edgecolors=red, linewidths=1.6, s=46,
                     zorder=4, label="AI outlier candidate")
        axis.set_title(f"{title} analysis", loc="left", fontsize=14, weight="bold", color=navy, pad=15)
        axis.set_title(f"{count} candidates | separate model", loc="right", fontsize=10, color=muted, pad=15)
        axis.set_ylabel(ylabel, color=muted, fontsize=10)
        axis.set_xlabel("Recorded time", color=muted, fontsize=9)
        axis.xaxis.set_major_locator(mdates.AutoDateLocator(minticks=3, maxticks=7))
        axis.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S", tz=stamps[0].tzinfo))
        axis.tick_params(colors=muted, labelsize=9)
        axis.grid(True, color="#E8EDF4", linewidth=0.7)
        axis.set_axisbelow(True)
        for name, spine in axis.spines.items():
            spine.set_visible(name in ("bottom", "left"))
            spine.set_color("#D5DDE8")
        axis.margins(x=0.025, y=0.2)
    temp_ax.axhline(LED_THRESHOLD, color="#94A3B8", linestyle="--", linewidth=1.2,
                       label="LED threshold: 25 C")
    for axis in (temp_ax, hum_ax):
        axis.legend(loc="upper left", fontsize=8, framealpha=0.95, edgecolor="#E2E8F0", ncol=3)
    hum_ax.set_ylim(0, 100)
    figure.text(0.085, 0.052, "EXPLORATORY DEMO", color=navy, fontsize=9, weight="bold")
    figure.text(0.085, 0.03,
                "Each model learns and scores the same recorded dataset. Candidates are not confirmed faults; accuracy is not evaluated.",
                color=muted, fontsize=9)
    plt.show()


def main():
    parser = argparse.ArgumentParser(description="EnviroGuard live charts and SQLite recording")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--history", action="store_true", help="Show the latest 25 saved measurements")
    modes.add_argument("--ai", action="store_true", help="Analyze temperature and humidity separately with Isolation Forest")
    args = parser.parse_args()
    if args.history:
        show_history()
        return
    if args.ai:
        show_ai()
        return
    print("Click Disconnect in Hercules. Find the ESP IP address in Serial Monitor.")
    ip = input("ESP IP address: ").strip()
    try:
        ipaddress.IPv4Address(ip)
    except ipaddress.AddressValueError:
        print("Enter a valid IPv4 address; example: 192.168.0.101")
        return

    events = queue.Queue()
    stop = threading.Event()
    worker = threading.Thread(target=collect, args=(ip, events, stop), daemon=True)
    times, temperatures, humidities = (deque(maxlen=300) for _ in range(3))

    figure, (temp_ax, hum_ax) = plt.subplots(2, 1, sharex=True, figsize=(10, 7))
    figure.canvas.manager.set_window_title("EnviroGuard - Live Charts")
    figure.suptitle("EnviroGuard | Temperature and Humidity")
    temp_line, = temp_ax.plot([], [], color="tab:green", label="Temperature")
    temp_ax.axhline(LED_THRESHOLD, color="tab:red", linestyle="--", label="LED threshold: 25 C")
    temp_ax.set_ylabel("Temperature (C)")
    temp_ax.legend(loc="upper left")
    hum_line, = hum_ax.plot([], [], color="tab:blue", label="Humidity")
    hum_ax.set_ylabel("Humidity (%)")
    hum_ax.set_xlabel("Time")
    hum_ax.set_ylim(0, 100)
    hum_ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S", tz=datetime.now().astimezone().tzinfo))
    for axis in (temp_ax, hum_ax):
        axis.grid(True, alpha=0.3)
    status_text = figure.text(0.08, 0.025, "Waiting for measurements...", fontsize=10)
    recording_text = figure.text(0.08, 0.055, "SQLite recording | Saved this session: 0", fontsize=10)
    figure.tight_layout(rect=(0, 0.07, 1, 0.94))

    saved_count = 0

    def update(_frame):
        nonlocal saved_count
        changed = False
        while True:
            try:
                kind, value = events.get_nowait()
            except queue.Empty:
                break
            if kind == "status":
                status_text.set_text(value)
                status_text.set_color("tab:orange")
            elif kind == "gap":
                if times and temperatures[-1] == temperatures[-1]:
                    times.append(datetime.now())
                    temperatures.append(float("nan"))
                    humidities.append(float("nan"))
                    changed = True
            else:
                stamp, temperature, humidity = value
                saved_count += 1
                recording_text.set_text(f"SQLite recording | Saved this session: {saved_count}")
                times.append(stamp)
                temperatures.append(temperature)
                humidities.append(humidity)
                hot = temperature >= LED_THRESHOLD
                color = "tab:red" if hot else "tab:green"
                temp_line.set_color(color)
                state = "HIGH TEMPERATURE (red LED)" if hot else "NORMAL (green LED)"
                status_text.set_text(f"{stamp:%H:%M:%S} | {temperature:.1f} C | {humidity:.1f}% | {state}")
                status_text.set_color(color)
                print(f"{stamp:%H:%M:%S}  Temperature: {temperature:.1f} C | Humidity: {humidity:.1f} %")
                changed = True
        if changed:
            temp_line.set_data(list(times), list(temperatures))
            hum_line.set_data(list(times), list(humidities))
            temp_ax.relim()
            temp_ax.autoscale_view(scalex=False)
            start = mdates.date2num(times[0])
            end = mdates.date2num(times[-1])
            margin = 2 / 86400
            hum_ax.set_xlim(start - margin, end + margin)
        return temp_line, hum_line, status_text, recording_text

    animation = FuncAnimation(figure, update, interval=250, cache_frame_data=False)
    figure.canvas.mpl_connect("close_event", lambda _event: stop.set())
    worker.start()
    try:
        plt.show()
    finally:
        stop.set()
        worker.join(timeout=6)


if __name__ == "__main__":
    main()
