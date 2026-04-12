import sys
import os
import random
from PySide6.QtWidgets import (
    QApplication, QWidget, QLabel, QPushButton,
    QVBoxLayout, QFileDialog, QHBoxLayout, QSpinBox
)
from PySide6.QtGui import QMovie
from PySide6.QtCore import Qt, QTimer


class GifPlayer(QWidget):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("随机 GIF 播放器")
        self.resize(400, 400)

        self.gif_list = []
        self.current_movie = None
        self.timer = QTimer(self)

        # UI
        self.label = QLabel("请选择GIF文件夹")
        self.label.setAlignment(Qt.AlignCenter)

        self.btn_select = QPushButton("选择文件夹")
        self.btn_next = QPushButton("下一张")
        self.btn_top = QPushButton("窗口置顶：关闭")

        self.interval_box = QSpinBox()
        self.interval_box.setRange(1, 60)
        self.interval_box.setValue(5)
        self.interval_box.setSuffix(" 秒")

        # 布局
        h_layout = QHBoxLayout()
        h_layout.addWidget(self.btn_select)
        h_layout.addWidget(self.btn_next)
        h_layout.addWidget(self.btn_top)

        bottom_layout = QHBoxLayout()
        bottom_layout.addWidget(self.interval_box)

        layout = QVBoxLayout()
        layout.addWidget(self.label)
        layout.addLayout(h_layout)
        layout.addLayout(bottom_layout)

        self.setLayout(layout)

        # 信号
        self.btn_select.clicked.connect(self.select_folder)
        self.btn_next.clicked.connect(self.play_random_gif)
        self.btn_top.clicked.connect(self.toggle_top)

        self.timer.timeout.connect(self.play_random_gif)

        self.interval_box.valueChanged.connect(self.update_timer)

    def select_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "选择GIF文件夹")
        if not folder:
            return

        self.gif_list = [
            os.path.join(folder, f)
            for f in os.listdir(folder)
            if f.lower().endswith(".gif")
        ]

        if not self.gif_list:
            self.label.setText("该目录没有GIF")
            return

        self.play_random_gif()
        self.update_timer()

    def play_random_gif(self):
        if not self.gif_list:
            return

        gif_path = random.choice(self.gif_list)

        if self.current_movie:
            self.current_movie.stop()

        self.current_movie = QMovie(gif_path)
        self.label.setMovie(self.current_movie)
        self.current_movie.start()

    def update_timer(self):
        interval = self.interval_box.value() * 1000
        self.timer.start(interval)

    def toggle_top(self):
        if self.windowFlags() & Qt.WindowStaysOnTopHint:
            self.setWindowFlag(Qt.WindowStaysOnTopHint, False)
            self.btn_top.setText("窗口置顶：关闭")
        else:
            self.setWindowFlag(Qt.WindowStaysOnTopHint, True)
            self.btn_top.setText("窗口置顶：开启")

        self.show()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = GifPlayer()
    window.show()
    sys.exit(app.exec())