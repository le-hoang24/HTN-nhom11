# Hệ thống vườn thông minh dùng ESP32

## Giới thiệu
Đây là dự án mô phỏng **hệ thống vườn thông minh** sử dụng **ESP32**, kết hợp **FreeRTOS**, **Blynk** và **LCD I2C** để theo dõi môi trường và điều khiển thiết bị tự động.

Hệ thống có thể:
- Đo **nhiệt độ** và **độ ẩm không khí** bằng cảm biến DHT11
- Đo **độ ẩm đất**
- Đo **cường độ ánh sáng**
- Phát hiện **chuyển động** bằng cảm biến PIR
- Điều khiển **relay bơm nước**
- Điều khiển **relay mái che**
- Hiển thị thông tin trên **LCD 16x2 I2C**
- Giám sát và điều khiển từ xa qua **Blynk**

---

## Công nghệ sử dụng
- ESP32
- Arduino IDE
- FreeRTOS
- Blynk
- DHT11
- LCD I2C
- Relay
- Cảm biến độ ẩm đất
- Cảm biến ánh sáng
- Cảm biến PIR

---

## Chức năng chính
### 1. Tưới nước tự động
- Khi độ ẩm đất xuống dưới ngưỡng cài đặt, hệ thống sẽ tự động bật bơm nước.
- Bơm chạy theo chu kỳ cài sẵn rồi tự tắt.
- Sau một khoảng thời gian nghỉ, hệ thống sẽ kiểm tra lại đất để quyết định có tưới tiếp hay không.

### 2. Điều khiển thủ công
- Người dùng có thể chuyển giữa **AUTO** và **MANUAL** trên Blynk.
- Ở chế độ **MANUAL**, người dùng có thể bật bơm bằng nút điều khiển trên ứng dụng.

### 3. Mái che tự động
- Hệ thống dùng cảm biến ánh sáng để đóng hoặc mở mái che.
- Có sử dụng ngưỡng đóng/mở khác nhau để tránh hiện tượng đóng mở liên tục.

### 4. Phát hiện chuyển động bằng PIR
- Nếu phát hiện chuyển động trong vùng hoạt động, hệ thống sẽ tạm thời chặn việc tưới.
- Nếu bơm đang chạy mà PIR phát hiện người hoặc vật chuyển động, bơm sẽ dừng để tăng độ an toàn.

### 5. Hiển thị và giám sát
- Dữ liệu được hiển thị trên LCD I2C theo từng trang.
- Đồng thời hệ thống gửi dữ liệu lên Blynk để theo dõi từ xa.

---

## Phần cứng sử dụng
- ESP32 DevKit V1
- Cảm biến DHT11
- Cảm biến độ ẩm đất
- Cảm biến ánh sáng
- Cảm biến PIR
- Relay bơm nước
- Relay mái che
- LCD I2C 16x2

---

## Kết nối chân
| Thiết bị | Chân ESP32 |
|---|---|
| DHT11 | GPIO 4 |
| Cảm biến độ ẩm đất | GPIO 34 |
| Cảm biến ánh sáng | GPIO 35 |
| PIR | GPIO 27 |
| Relay bơm | GPIO 26 |
| Relay mái che | GPIO 25 |
| LCD SDA | GPIO 21 |
| LCD SCL | GPIO 22 |

---

## Thư viện cần cài
Cài trong **Library Manager** của Arduino IDE:
- **Blynk**
- **DHT sensor library** by Adafruit
- **LiquidCrystal_I2C**

---

## Cấu hình Blynk
Dự án dùng các Virtual Pin sau:
- `V0`: Nhiệt độ
- `V1`: Độ ẩm không khí
- `V2`: Độ ẩm đất
- `V3`: Ánh sáng
- `V4`: Trạng thái bơm
- `V5`: Trạng thái mái che
- `V6`: Chế độ AUTO / MANUAL
- `V7`: Trạng thái hoặc số lần phát hiện chuyển động
- `V8`: Nút bơm thủ công

---

## Cách nạp chương trình
1. Mở dự án trong **Arduino IDE**.
2. Cài đầy đủ các thư viện cần thiết.
3. Chọn đúng board: **DOIT ESP32 DEVKIT V1**.
4. Chỉnh lại thông tin:
   - `BLYNK_TEMPLATE_ID`
   - `BLYNK_TEMPLATE_NAME`
   - `BLYNK_AUTH_TOKEN`
   - `WIFI_SSID`
   - `WIFI_PASS`
5. Nạp code vào ESP32.
6. Mở **Serial Monitor** để kiểm tra hoạt động.

---

## Gợi ý hiệu chỉnh
### Cảm biến độ ẩm đất
Cần hiệu chỉnh lại các giá trị:
- `SOIL_ADC_DRY`
- `SOIL_ADC_WET`

### Cảm biến ánh sáng
Cần theo dõi giá trị ADC thực tế rồi chỉnh:
- `LIGHT_BRIGHT_IS_HIGH`
- `LIGHT_CLOSE_THRESHOLD`
- `LIGHT_OPEN_THRESHOLD`

### PIR
Có thể cần chỉnh lại:
- độ nhạy
- thời gian giữ tín hiệu
- vị trí đặt cảm biến

---

## Lưu ý khi public lên GitHub
Nếu bạn để repo ở chế độ **public**, nên:
- Xóa hoặc ẩn **WiFi SSID / mật khẩu WiFi**
- Xóa hoặc đổi mới **Blynk Auth Token**
- Không đưa thông tin nhạy cảm trực tiếp vào mã nguồn

---

## Thành viên nhóm
- Võ Quang Thiềm - 106220112
- Trần Văn Đức - 106220009
- Lê Văn Minh Hoàng - 106220134
- Trần Minh Thông - 106220154

---

## Hướng phát triển
- Tối ưu phần phát hiện chuyển động bằng PIR
- Cải thiện độ ổn định nguồn cho relay và bơm
- Bổ sung điều kiện điều khiển theo nhiệt độ
- Hoàn thiện giao diện Blynk trực quan hơn
- Mở rộng sang giám sát nhiều khu vực trồng
