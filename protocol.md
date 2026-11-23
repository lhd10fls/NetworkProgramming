# Giao thức văn bản Online Test System

## 1. Phạm vi

Client và server dùng TCP/IPv4, mặc định cổng 9000. TCP vẫn vận chuyển luồng byte; tầng ứng dụng trao đổi chuỗi UTF-8 có kết thúc dòng, không dùng tiền tố độ dài nhị phân. Server dùng SQLite question.db; mỗi tài khoản chỉ có một phiên trên một server.

## 2. Ranh giới và cú pháp

Mỗi yêu cầu hoặc phản hồi là một dòng kết thúc bằng hai byte CR LF (`\r\n`). Không gửi chữ `\r\n` nguyên văn. Giới hạn dòng nội dung trên mạng là 65.536 byte, chưa tính CRLF. Bên nhận đọc đến CRLF; dòng thiếu kết thúc vẫn phải chờ, mất kết nối thì kết thúc. Dòng vượt giới hạn, NUL, LF đơn hoặc CR không đi với LF làm đóng kết nối.

Các trường phân tách bằng một dấu cách. Có thể đặt tham số trong dấu ngoặc kép; server luôn đặt dữ liệu phản hồi trong ngoặc kép. Dùng `\"` cho dấu ngoặc kép, `\\` cho dấu gạch chéo ngược. Trường rỗng viết `""`. Tên lệnh và phạm vi viết HOA. Số được truyền dưới dạng chữ số thập phân, có thể đặt trong ngoặc kép. end_at là Unix timestamp theo giây do server quyết định.

Ví dụ yêu cầu thực tế: `JOIN ROOM PhongA\r\n`; phản hồi `122\r\n`. LIST ROOMS trả danh sách phòng, không phải danh sách người dùng. room_name luôn là tên phòng thi; practice_id luôn là mã bài luyện tập cá nhân.

Phản hồi danh sách hoặc đề dùng từ khóa ROW để bắt đầu mỗi bản ghi, vẫn nằm trong cùng một dòng CRLF. ROW là từ khóa cấu trúc không có ngoặc kép; văn bản "ROW" có ngoặc kép vẫn là dữ liệu. Trong nội bộ C, net.c chuyển qua lại giữa chuỗi trường phân cách bằng | và dòng giao thức. Ký tự | chỉ là biểu diễn nội bộ, không truyền trên socket. Dữ liệu nhập không chứa |, CR, LF hoặc ký tự điều khiển. Giới hạn trường được tính theo byte UTF-8, không theo số ký tự.

## 3. Mã phản hồi và lỗi chung

| Mã | Ý nghĩa / dữ liệu theo sau |
|---|---|
| 110 | Đăng ký thành công |
| 111 | Đăng nhập thành công, không có trường vai trò |
| 112 | Đăng xuất thành công |
| 120 | Tạo phòng thành công, trạng thái ban đầu WAITING |
| 121 | Danh sách phòng: các ROW R room_name state count duration owner |
| 122 | Tham gia phòng thành công |
| 123 | Bắt đầu phòng thành công |
| 124 | Phòng vẫn WAITING sau lượt chờ |
| 125 | Đề phòng: room_name end_at count, các ROW Q pos text A B C D selected |
| 126 | Lưu lựa chọn thành công, dùng chung hai chế độ |
| 127 | Điểm cá nhân của phòng: correct total |
| 128 | Trạng thái phòng: state end_at |
| 129 | Bảng điểm: các ROW R username correct total automatic |
| 130 | Phòng đã kết thúc: correct total |
| 131 | Tạo bài cá nhân: practice_id end_at count |
| 132 | Câu luyện tập: pos text A B C D |
| 133 | Điểm luyện tập: correct total |
| 134 | Trạng thái bài cá nhân: RUNNING end_at |
| 210 | Sự kiện hết giờ phòng: room_name correct total |
| 400 | Sai yêu cầu/lệnh, theo sau là lý do |
| 401 | Chưa xác thực hoặc sai thông tin đăng nhập |
| 403 | Không có quyền |
| 404 | Không tìm thấy dữ liệu |
| 409 | Xung đột hoặc sai trạng thái nghiệp vụ |
| 500 | Lỗi xử lý phía server |

Mã lỗi dùng chung cho mọi nhóm lệnh. Ví dụ `400 BAD_REQUEST`, `400 UNKNOWN_COMMAND`, `401 LOGIN_REQUIRED`, `401 CREDENTIALS`, `403 FORBIDDEN`, `403 NOT_PARTICIPANT`, `404 ROOM_NOT_FOUND`, `404 PRACTICE_NOT_FOUND`, `409 USER_EXISTS`, `409 ROOM_EXISTS`, `409 ALREADY_LOGGED_IN`, `409 ROOM_NOT_WAITING`, `409 ROOM_FULL`, `409 NO_PARTICIPANTS`, `409 EXAM_CLOSED`, `409 PRACTICE_CLOSED`, `409 ROOM_NOT_FINISHED`, `409 NOT_ENOUGH_QUESTIONS`, `409 NOT_RUNNING`, `409 RESULT_NOT_READY`, `500 DB`, `500 PASSWORD_HASH`. Client dùng mã số để phân nhóm và reason để giải thích. Các lỗi truyền tải có thể đóng kết nối mà không trả mã.

## 4. Hành động, trạng thái và quyền

AWAIT là hành động chờ phản hồi, WAITING là trạng thái phòng. Không dùng WAIT làm tên lệnh trên mạng. Trạng thái phòng là WAITING, RUNNING, FINISHED. REGISTER không tự đăng nhập. LOGIN thành công gắn user_id/username vào socket; LOGOUT hoặc ngắt kết nối giải phóng phiên.

Bất kỳ tài khoản đã đăng nhập đều được CREATE ROOM. Chỉ người tạo phòng được START ROOM; người dùng khác không được bắt đầu phòng này. Chỉ thành viên được AWAIT/QUESTIONS/ANSWER/SUBMIT/STATUS ROOM. RESULTS ROOM chỉ cho chủ phòng hoặc thành viên, sau FINISHED. Lệnh PRACTICE theo phạm vi chỉ truy cập bài của chính tài khoản đó. Không còn chức năng thêm câu hỏi và đổi mật khẩu qua giao thức/menu. Ngân hàng mẫu được quản lý trong seed_questions.c.

AWAIT giữ yêu cầu khoảng tối đa 30 giây. Server kiểm tra trạng thái mỗi 100 ms; chưa bắt đầu thì trả 124 và client gửi AWAIT lại. Khi RUNNING, trả 125. Khi FINISHED, trả 130 hoặc lỗi nếu chưa có kết quả. Gửi một yêu cầu và đợi phản hồi chính trước yêu cầu kế tiếp, không có request ID. Sự kiện 210 có thể xen giữa; receiver_thread nhận diện riêng.

## 5. Danh mục hành động

### 5.1. Tài khoản

| Yêu cầu | Phản hồi thành công |
|---|---|
| REGISTER username password | 110 |
| LOGIN username password | 111 |
| LOGOUT | 112 |

Username tối đa 32 byte; password đăng ký tối đa 128 byte; tên không trùng trong cùng database. Hiện chưa có TLS; mật khẩu chỉ được băm khi lưu.

### 5.2. Phòng thi và bài luyện tập dùng chung ngân hàng

Không còn nhóm lệnh PQUESTION/PANSWER/PSUBMIT/PSTATUS trên mạng; cũng không có lệnh PRACTICE độc lập. Cùng một hành động dùng phạm vi ROOM hoặc PRACTICE để chỉ rõ đối tượng. Hai chế độ cùng lấy từ bảng questions nhưng có lịch thi và quyền riêng.

| Yêu cầu | Ý nghĩa và phản hồi |
|---|---|
| CREATE ROOM room_name count duration | Chọn cố định đề, tạo phòng WAITING; 120 |
| CREATE PRACTICE count duration | Tạo bài cá nhân và bắt đầu tính giờ ngay; 131 practice_id end_at count |
| LIST ROOMS | Danh sách tối đa 100 phòng mới nhất; 121 và các ROW |
| JOIN ROOM room_name | Vào phòng WAITING, tối đa 32 người; 122 |
| START ROOM room_name | Chủ phòng, có ít nhất một thành viên; 123 |
| AWAIT ROOM room_name | Chờ phòng bắt đầu; 124 / 125 / 130 |
| QUESTIONS ROOM room_name | Lấy lại toàn bộ đề khi RUNNING; 125; phòng chờ trả 124, kết thúc trả 130 |
| QUESTIONS PRACTICE practice_id pos | Lấy một câu thuộc bài cá nhân còn giờ; 132 |
| ANSWER ROOM room_name pos choice | Lưu/thay đáp án nếu còn giờ và chưa nộp; 126 |
| ANSWER PRACTICE practice_id pos choice | Lưu/thay đáp án của bài cá nhân còn giờ; 126 |
| SUBMIT ROOM room_name | Nộp sớm hoặc lấy lại điểm đã lưu; 127 |
| SUBMIT PRACTICE practice_id | Nộp sớm hoặc lấy lại điểm; 133 |
| STATUS ROOM room_name | 128 state end_at hoặc 127 correct total |
| STATUS PRACTICE practice_id | 134 RUNNING end_at hoặc 133 correct total |
| RESULTS ROOM room_name | Bảng điểm khi FINISHED; 129 và các ROW |

room_name: tên phòng thi, không phải username hay topic; tối đa 48 byte. count: 1–50 và không vượt ngân hàng. duration: 10–86400 giây. pos: bắt đầu từ 1 và nằm trong đề. choice: A/B/C/D. Ngân hàng mặc định có 20 câu; không thể tạo đề 50 câu nếu chưa đủ dữ liệu. Giao thức cho đổi lựa chọn luyện tập nhưng UI hiện đi lần lượt, chưa có nút quay lại câu trước.

## 6. Ví dụ dòng phản hồi

Các ví dụ dưới đây đều phải thêm CRLF ở cuối khi gửi. Không có dấu | trên mạng.

```text
111
121 ROW "R" "PhongA" "WAITING" "2" "60" "teacher"
125 "PhongA" "1795000000" "2" ROW "Q" "1" "2 + 3 bằng bao nhiêu?" "4" "5" "6" "7" "" ROW "Q" "2" "Một tuần có mấy ngày?" "5" "7" "6" "8" "B"
127 "1" "2"
129 ROW "R" "alice" "1" "2" "0"
210 "PhongA" "1" "2"
409 ROOM_NOT_WAITING
```

Trường cuối của ROW Q là lựa chọn người thi đã lưu, không phải đáp án đúng. automatic bằng 0 khi nộp sớm, 1 khi tự chấm hết giờ. 210 chỉ áp dụng thi theo phòng; bài cá nhân tự lưu điểm và client hỏi STATUS/SUBMIT để nhận 133.

## 7. Trình tự và thời gian

Thi theo phòng: LOGIN → CREATE ROOM → JOIN ROOM → AWAIT ROOM → START ROOM → 125 đề → ANSWER ROOM → SUBMIT ROOM → 127 điểm → RESULTS ROOM → 129 bảng điểm. AWAIT được gửi bởi người thi; START được gửi bởi chủ phòng trên kết nối khác. 125 là phản hồi của AWAIT đang chờ. Thời gian bắt đầu khi server xử lý START, trước khi client nhận đề. Khi mọi người nộp sớm, hoặc khi end_at đến hạn, phòng chuyển FINISHED. Hết giờ server tự chấm người chưa nộp, lưu SQLite và gửi 210 nếu còn kết nối. Người mất kết nối có thể LOGIN rồi STATUS để lấy điểm.

Luyện tập: LOGIN → CREATE PRACTICE → 131 ID/hạn giờ → QUESTIONS PRACTICE → 132 câu → ANSWER PRACTICE → 126 → lặp câu tiếp → SUBMIT PRACTICE → 133 điểm. Thời gian bắt đầu ngay khi tạo bài, không có JOIN, AWAIT hoặc START. Câu và thời gian tải qua mạng nằm trong thời lượng bài. Đồng hồ server quyết định hạn nộp; đồng hồ client chỉ phục vụ giao diện.

## 8. Triển khai và kiểm chứng

net.c/net.h thực hiện codec văn bản, quoting, ROW và CRLF. Chuỗi phân trường nội bộ chỉ để tái sử dụng phần nghiệp vụ; request/response trên socket tuân theo tài liệu này. server.c giữ máy trạng thái, quyền và SQLite; client.c giữ menu và luồng nhận sự kiện. Bộ kiểm thử tests/test_integration.py gửi trực tiếp giao thức mới, kiểm tra dữ liệu chia nhỏ, nhiều dòng gộp một lần gửi, quyền, tạo/thi/nộp, luyện tập, sự kiện hết giờ, UTF-8 và client thật. Giao thức mới không tương thích client/server phiên bản cũ; cần biên dịch cả hai phía.

## Tài khoản thống nhất và tương thích database

Ứng dụng đã bỏ vai trò admin/student và EXAM_ADMIN_CODE khỏi đăng ký, đăng nhập, menu và kiểm tra quyền. REGISTER chỉ nhận username password, LOGIN trả 111 không có tham số role. Quyền phụ thuộc người tạo phòng, thành viên phòng hoặc chủ bài cá nhân. Cột users.role được giữ như dữ liệu tương thích với database cũ; tài khoản mới ghi giá trị legacy student để đáp ứng ràng buộc cũ, không dùng cột này để phân quyền. Tài khoản admin cũ vẫn đăng nhập được như mọi tài khoản. Không xóa hoặc ghi đè question.db.
