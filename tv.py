import sys
import os
import requests
import datetime
import hashlib
from PySide6.QtWidgets import QApplication, QWidget, QLabel, QVBoxLayout, QPushButton, QHBoxLayout
from PySide6.QtGui import QMovie
from PySide6.QtCore import Qt, QTimer

# ====== 配置 ======
API_KEY = "66afdde4ffca307e671dc28a3a2d66d2"
CITY = "Hangzhou"
birthday = "1998-09-03"
sign = "virgo"


# 天气映射
WEATHER_MAP = {
    "Clear": "finaloutput_sunny.gif",
    "Clouds": "finaloutput_wind.gif",
    "Rain": "finaloutput_rain.gif",
    "Snow": "finaloutput_snow.gif",
    "Drizzle": "finaloutput_rain.gif",
    "Thunderstorm": "finaloutput_thunder.gif"
}

LEVELS = ["Terrible", "Bad", "Average", "Good", "Excellent"]
DIVINATION_MAP = {
    "Terrible": "finaloutput_Skull.gif",
    "Bad": "finaloutput_bat.gif",
    "Average": "finaloutput_Fairy_Light.gif",
    "Good": "finaloutput_gold.gif",
    "Excellent": "finaloutput_star.gif"
}

# button_style = """
# QPushButton {
#     background-color: #444;
#     color: white;
#     border-radius: 8px;
#     padding: 10px;
#     font-size: 16px;
# }

# QPushButton:hover {
#     background-color: #66ccff;
#     color: black;
#     border: 2px solid white;
# }

# QPushButton:pressed {
#     background-color: #3399cc;
# }
# """

button_style = """
QPushButton {
    background-color: #3a2f1b;
    color: #f5e6c8;
    border: 3px solid #8b6f3d;
    border-radius: 6px;
    padding: 12px;
    font-size: 16px;
}

QPushButton:hover {
    background-color: #caa86b;
    color: black;
    border: 3px solid #fff2cc;
}

QPushButton:pressed {
    background-color: #a67c3d;
    padding-left: 14px;
    padding-top: 14px;
}
"""


def get_weather():
    url = f"https://api.openweathermap.org/data/2.5/weather?q={CITY}&appid={API_KEY}"
    try:
        res = requests.get(url, timeout=5)
        data = res.json()
        return data["weather"][0]["main"]
    except:
        return None
    
def normalize_weather(text):
    if "晴" in text or text == "Clear":
        return "Clear"
    if "云" in text or "雾" in text or "阴" in text or text == "Clouds":
        return "Clouds"
    if "雨" in text or text == "Rain":
        return "Rain"
    if "雪" in text or text == "Snow":
        return "Snow"
    if "雷" in text or text == "Thunderstorm":
        return "Thunderstorm"
    return None

def get_weather_in_China():
    key = "07058f80bc1f4f82812f1209abdad557"
    city = "101210101"  # 杭州

    url = f"https://mg4y3m26vm.re.qweatherapi.com/v7/weather/now?location={city}&key={key}"

    try:
        res = requests.get(url, timeout=5)
        data = res.json()
        #print(data)

        if data.get("code") != "200":
            print("API错误:", data)
            return None

        return normalize_weather(data["now"]["text"])

    except Exception as e:
        print("请求失败:", e)
        return None

def calc_seed(birthday, sign):
    today = datetime.date.today().isoformat()
    key = f"{birthday}-{sign}-{today}"
    hash_val = hashlib.md5(key.encode()).hexdigest()
    return int(hash_val[:8], 16)

def calc_luck_level(seed):
    return seed % 5

def get_luck(birthday, sign):
    seed = calc_seed(birthday, sign)
    level = calc_luck_level(seed)
    return level, DIVINATION_MAP[LEVELS[level]]

class GifPlayer(QWidget):
    def __init__(self):
        super().__init__()

        # self.setStyleSheet("""
        # QWidget {
        #     background-color: #222;
        #     color: white;
        # }
        # """)

        self.setStyleSheet("""
        QWidget {
            background-color: #1b1b1b;
            color: #f5f5dc;
            font-family: "Courier New";  /* 像素感字体 */
        }

        QLabel {
            font-size: 18px;
        }
        """)

        self.setWindowTitle("📺 星露谷小电视")
        self.resize(400, 400)

        # 当前目录下 gifs 文件夹
        self.folder = os.path.join(os.getcwd(), "gifs")

        self.started = False  # 👈 关键状态
        self.mode = "home"  # 👈 当前模式

         # ===== UI =====
        self.label = QLabel("📺 欢迎收看 今日节目")
        self.label.setAlignment(Qt.AlignCenter)

        self.btn_weather = QPushButton("天气预报")
        self.btn_luck = QPushButton("算命占卜")
        self.btn_back = QPushButton("返回首页")

        self.btn_weather.setStyleSheet(button_style)
        self.btn_luck.setStyleSheet(button_style)
        self.btn_back.setStyleSheet(button_style)

        # 默认隐藏返回按钮
        self.btn_back.hide()

        # 布局
        btn_layout = QVBoxLayout()
        btn_layout.setSpacing(15)
        btn_layout.addWidget(self.btn_weather)
        btn_layout.addWidget(self.btn_luck)

        layout = QVBoxLayout()
        layout.setSpacing(10)
        layout.setContentsMargins(40, 40, 40, 40)
        layout.addWidget(self.label)
        layout.addLayout(btn_layout)
        layout.addWidget(self.btn_back)
        self.setLayout(layout)

         # ===== 信号 =====
        self.btn_weather.clicked.connect(self.start_weather)
        self.btn_luck.clicked.connect(self.start_luck)
        self.btn_back.clicked.connect(self.go_home)

        self.current_movie = None
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.play_weather_gif)

        # ===== 首页 =====
    def go_home(self):
        self.mode = "home"

        if self.current_movie:
            self.current_movie.stop()

        self.timer.stop()

        self.label.setText("📺 欢迎收看 今日节目")

        self.btn_weather.show()
        self.btn_luck.show()
        self.btn_back.hide()

        self.setWindowTitle(f"📺 星露谷小电视")

    # ===== 天气模式 =====
    def start_weather(self):
        self.mode = "weather"

        self.btn_weather.hide()
        self.btn_luck.hide()
        self.btn_back.show()

        self.play_weather_gif()
        self.timer.start(30 * 60 * 1000)

        self.setWindowTitle(f"天气预报")

    # ===== 运气占卜 =====
    def start_luck(self):
        self.mode = "luck"

        self.btn_weather.hide()
        self.btn_luck.hide()
        self.btn_back.show()

        self.timer.stop()  # 占卜不需要定时刷新

        # 根据日期生成“稳定随机”
        level, gif_name = get_luck(birthday,sign)
        self.play_gif(gif_name)
        self.setWindowTitle(f"算命占卜")

        #self.setWindowTitle(f"今日运势：{LEVELS[level]}")

        # ===== 通用播放 =====
    def play_gif(self, gif_name):
        gif_path = os.path.join(self.folder, gif_name)

        if not os.path.exists(gif_path):
            self.label.setText(f"缺少GIF: {gif_name}")
            return

        if self.current_movie:
            self.current_movie.stop()

        self.current_movie = QMovie(gif_path)
        self.label.setMovie(self.current_movie)
        self.current_movie.start()

    def play_weather_gif(self):
        weather = get_weather_in_China()

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