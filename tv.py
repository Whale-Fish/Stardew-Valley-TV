import sys
import os
import requests
from PySide6.QtWidgets import QApplication, QWidget, QLabel, QVBoxLayout, QPushButton
from PySide6.QtGui import QMovie
from PySide6.QtCore import Qt, QTimer

# ====== 配置 ======
API_KEY = "66afdde4ffca307e671dc28a3a2d66d2"
CITY = "Hangzhou"

# 天气映射
# WEATHER_MAP = {
#     "Clear": "sunny.gif",
#     "Clouds": "wind.gif",
#     "Rain": "rain.gif",
#     "Snow": "snow.gif",
#     "Drizzle": "rain.gif",
#     "Thunderstorm": "thunder.gif"
# }
WEATHER_MAP = {
    "Clear": "finaloutput_sunny.gif",
    "Clouds": "finaloutput_wind.gif",
    "Rain": "finaloutput_rain.gif",
    "Snow": "finaloutput_snow.gif",
    "Drizzle": "finaloutput_rain.gif",
    "Thunderstorm": "finaloutput_thunder.gif"
}


def get_weather():
    url = f"https://api.openweathermap.org/data/2.5/weather?q={CITY}&appid={API_KEY}"
    try:
        res = requests.get(url, timeout=5)
        data = res.json()
        return data["weather"][0]["main"]
    except:
        return None


class GifPlayer(QWidget):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("天气 GIF 播放器")
        self.resize(400, 400)

        # 当前目录下 gifs 文件夹
        self.folder = os.path.join(os.getcwd(), "gifs")

        self.started = False  # 👈 关键状态

         # ===== UI =====
        self.label = QLabel("点击下方按钮查看天气")
        self.label.setAlignment(Qt.AlignCenter)

        self.btn_weather = QPushButton("天气预报")

        layout = QVBoxLayout()
        layout.addWidget(self.label)
        layout.addWidget(self.btn_weather)
        self.setLayout(layout)

        # ===== 信号 =====
        self.btn_weather.clicked.connect(self.on_click)

        self.current_movie = None
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.play_weather_gif)

    # ===== 按钮点击 =====
    def on_click(self):
        if not self.started:
            self.started = True
            self.start_player()
        else:
            # 可扩展：以后做“返回首页”或“刷新”
            self.play_weather_gif()

        # ===== 启动播放逻辑 =====
    def start_player(self):
        self.play_weather_gif()
        self.timer.start(30 * 60 * 1000)  # 30分钟更新

    def play_weather_gif(self):
        weather = get_weather()

        if not weather:
            self.label.setText("天气获取失败")
            return
            #weather = "Clear"

        gif_name = WEATHER_MAP.get(weather)

        if not gif_name:
            self.label.setText(f"未知天气: {weather}")
            return

        gif_path = os.path.join(self.folder, gif_name)

        if not os.path.exists(gif_path):
            self.label.setText(f"缺少GIF: {gif_name}")
            return

        if self.current_movie:
            self.current_movie.stop()

        self.current_movie = QMovie(gif_path)
        self.label.setMovie(self.current_movie)
        self.current_movie.start()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = GifPlayer()
    window.show()
    sys.exit(app.exec())