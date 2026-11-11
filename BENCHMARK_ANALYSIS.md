# Phân tích tiêu chí đánh giá — Online Test System (đề tài 10)

Tài liệu này đối chiếu **13 tiêu chí, tổng 20 điểm** ở cột **Yêu cầu** của dòng đề tài số 10 trong file **IT4062_Network_Programing_Project_Topic_TungBT.xlsx** với mã nguồn của thư mục **NetProgramming-complete**. Các số dòng dưới đây ứng với phiên bản mã nguồn hiện tại, sau khi bổ sung **seed_questions.c**. Nếu sửa code, cần kiểm tra lại số dòng.

**Cách đọc trạng thái:** “Đã triển khai” nghĩa là có nhánh xử lý thực tế trong code. “Đã kiểm thử” nghĩa là có phép kiểm tra tương ứng trong **tests/test_integration.py** và bộ kiểm thử đã chạy thành công trên Windows ngày 11/11/2026. Đây là bằng chứng kỹ thuật, không phải cam kết điểm chính thức; giảng viên quyết định điểm theo cách vận hành và tiêu chuẩn chấm của môn học.

## Tóm tắt nhanh

| STT | Tiêu chí trong file Excel | Điểm tối đa | Vị trí chính trong code | Đánh giá |
|---:|---|---:|---|---|
| 1 | Xử lý truyền dòng | 1 | net.c:32–63, net.h:22, client.c:25–48, server.c:543–582 | Đã triển khai và kiểm thử |
| 2 | Cài đặt cơ chế vào/ra socket trên server | 2 | server.c:543–623, net.c:10–63 | Đã triển khai và kiểm thử |
| 3 | Đăng ký và quản lý tài khoản | 2 | server.c:238–252, 268–290; password.c:17–81 | Đã triển khai và kiểm thử |
| 4 | Đăng nhập và quản lý phiên | 2 | server.c:21–28, 254–272, 543–588 | Đã triển khai và kiểm thử |
| 5 | Kiểm soát quyền truy cập | 1 | server.c:266–267, 293–295, 335–357, 371–390, 444–451 | Đã triển khai và kiểm thử |
| 6 | Tham gia chế độ luyện tập | 1 | server.c:460–529, 198–211; client.c:259–283 | Đã triển khai và kiểm thử |
| 7 | Tạo phòng thi | 2 | server.c:305–324; seed_questions.c:9–63 | Đã triển khai và kiểm thử |
| 8 | Xem danh sách phòng thi | 1 | server.c:326–333; client.c:121–134 | Đã triển khai và kiểm thử |
| 9 | Tham gia vào phòng thi | 2 | server.c:335–354; client.c:243–258 | Đã triển khai và kiểm thử |
| 10 | Bắt đầu bài thi | 1 | server.c:356–386, 215–231, 553–566 | Đã triển khai và kiểm thử |
| 11 | Nộp bài | 3 | server.c:127–211, 388–441; client.c:25–48, 191–242 | Đã triển khai và kiểm thử |
| 12 | Xem kết quả thi của các phòng thi | 1 | server.c:444–458; client.c:150–166 | Đã triển khai và kiểm thử |
| 13 | Ghi log hoạt động | 1 | server.c:53–60, 100, các lời gọi log_event | Đã triển khai và kiểm thử một phần |
| | **Tổng tiêu chí có code tương ứng** | **20** | **13/13 tiêu chí** | **Điểm thực tế do giảng viên chấm** |

## Kiến trúc và cách đối chiếu

- **Client:** client.c tạo giao diện dòng lệnh, kết nối TCP, gửi lệnh và hiển thị phản hồi. Nó không giữ đáp án đúng của ngân hàng câu hỏi.
- **Server:** server.c mở cổng, tạo một luồng cho mỗi kết nối, kiểm tra đăng nhập/quyền, xử lý lệnh và chấm điểm.
- **Truyền dữ liệu:** net.c/net.h đóng khung từng thông điệp TCP bằng 4 byte độ dài theo network byte order, sau đó là nội dung.
- **Dữ liệu:** SQLite lưu tài khoản, câu hỏi, phòng, người tham gia, bài làm, kết quả, bài luyện tập và log. Các bảng được tạo tại server.c:86–101.
- **Mật khẩu:** password.c dùng PBKDF2-HMAC-SHA256 với salt ngẫu nhiên, 200.000 vòng.
- **Câu hỏi mẫu:** seed_questions.c nạp 20 câu vào bảng questions khi bảng đang rỗng; không nhân đôi khi server khởi động lại. Có thể tắt nạp mẫu bằng EXAM_SKIP_SEED=1.

### 1. Xử lý truyền dòng — 1 điểm

**Yêu cầu:** phân định đúng các thông điệp trên luồng byte TCP, kể cả khi một thông điệp đến thành nhiều phần hoặc nhiều thông điệp đến cùng một lần nhận.

**Vị trí code:**

- **net.c:32–46** — send_all và recv_all lặp cho đến khi đủ số byte cần gửi/nhận; không giả định một lệnh send hoặc recv xử lý hết thông điệp.
- **net.c:48–63** — send_frame gửi 4 byte độ dài rồi gửi nội dung; recv_frame đọc đủ 4 byte, đổi byte order, giới hạn MAX_FRAME và đọc đúng số byte đã khai báo.
- **net.h:22** — giới hạn khung là 65.536 byte.
- **client.c:74–87** và **server.c:543–579** — hai phía đều dùng send_frame/recv_frame cho toàn bộ lệnh và phản hồi.
- **net.c:64–70** — valid_field chặn dấu phân cách và ký tự điều khiển trong các trường người dùng nhập.

**Dòng dữ liệu:** ví dụ nội dung ANSWER|PhongA|2|B được gửi với 4 byte ghi số byte của nội dung. Server đọc độ dài trước, sau đó đọc đúng phần còn lại; không dựa vào ký tự xuống dòng để kết thúc khung. Sau khi nhận trọn khung, **server.c:74–84** mới tách trường theo dấu |.

**Kiểm chứng:** **tests/test_integration.py:122** gửi một khung thành từng byte; **:130–134** gửi hai khung liền nhau trong một lần ghi và đòi đúng hai phản hồi. Cả hai trường hợp đã chạy qua.

**Giới hạn:** đây là xử lý ranh giới thông điệp, không phải mã hóa kết nối. TCP hiện chưa có TLS.

### 2. Cài đặt cơ chế vào/ra socket trên server — 2 điểm

**Yêu cầu:** server nhận nhiều kết nối và trao đổi dữ liệu qua socket.

**Vị trí code:**

- **server.c:590–610** — main nhận cổng, khởi tạo mạng/database, gọi socket với AF_INET + SOCK_STREAM, setsockopt, bind và listen.
- **server.c:611–620** — vòng lặp accept; cấp phát cấu trúc Client, tạo pthread cho kết nối mới và detach luồng.
- **server.c:543–588** — client_thread nhận khung, xử lý lệnh, gửi phản hồi và dọn kết nối khi client rời đi.
- **server.c:21–28** — mỗi Client giữ socket, user_id, username, role và mutex riêng khi gửi.
- **server.c:18, 32–37, 558–580** — db_mutex bảo vệ dữ liệu dùng chung; send_mutex ngăn hai luồng gửi chồng dữ liệu trên cùng socket.
- **net.c:10–30** — khởi tạo WinSock trên Windows hoặc xử lý SIGPIPE trên Linux, cùng hàm ngủ tương ứng hệ điều hành.

**Kiểm chứng:** **tests/test_integration.py:115–133** kết nối đồng thời admin, student và stranger; các phần sau dùng nhiều client để tạo/vào phòng, chờ đề và nhận thông báo hết giờ. Bài kiểm thử dùng chính server.exe, không dùng server giả.

**Giới hạn:** server dùng một luồng cho mỗi client và một mutex database chung; phù hợp bài tập/lab, chưa được benchmark tải lớn. Mảng active_clients giới hạn 256 kết nối đang được theo dõi.

### 3. Đăng ký và quản lý tài khoản — 2 điểm

**Yêu cầu:** tạo tài khoản, quản lý thông tin đăng nhập.

**Vị trí code:**

- **server.c:90** — bảng users có username duy nhất, password và role chỉ nhận admin/student.
- **server.c:238–252** — REGISTER kiểm tra số trường, độ dài tên/mật khẩu, vai trò, mã admin, trùng username rồi lưu tài khoản.
- **server.c:274–290** — CHPASS kiểm tra mật khẩu cũ, băm mật khẩu mới và cập nhật database.
- **server.c:268–272** — LOGOUT xóa trạng thái đăng nhập trên kết nối.
- **password.c:17–38, 61–81** — sinh salt ngẫu nhiên, tạo và xác minh giá trị PBKDF2-HMAC-SHA256; Windows dùng BCrypt, Linux dùng OpenSSL.
- **client.c:99–119, 297–303** — các màn hình đăng ký, đăng nhập và đổi mật khẩu.

**Dòng dữ liệu:** client gửi REGISTER|username|password|role|admin_code. Server không lưu mật khẩu gốc; cột password có dạng pbkdf2$iterations$salt$hash. Username trùng bị từ chối. Admin cần mã lấy từ EXAM_ADMIN_CODE; nếu không đặt, server.c:594–595 dùng giá trị mặc định admin123.

**Kiểm chứng:** **tests/test_integration.py:122–128** đăng ký và đăng nhập ba tài khoản; **:129** kiểm tra mật khẩu lưu dạng pbkdf2; **:228–231** đổi mật khẩu, xác nhận mật khẩu cũ bị từ chối và mật khẩu mới dùng được.

**Giới hạn:** mật khẩu đi qua TCP chưa mã hóa; chỉ nên dùng trong mạng tin cậy khi demo. Mã admin mặc định nên đổi trước khi trình bày.

### 4. Đăng nhập và quản lý phiên — 2 điểm

**Yêu cầu:** xác thực người dùng và gắn quyền với phiên làm việc.

**Vị trí code:**

- **server.c:254–265** — LOGIN truy vấn users theo username, gọi password_verify, gắn user_id/role/username vào cấu trúc Client hoặc trả ERR|CREDENTIALS.
- **server.c:266–267** — các lệnh còn lại bị từ chối bằng ERR|LOGIN_REQUIRED nếu chưa có user_id.
- **server.c:268–272** — LOGOUT xóa thông tin phiên.
- **server.c:543–588** — khi kết nối TCP đóng, luồng xóa Client khỏi danh sách đang hoạt động và ghi DISCONNECT nếu đã đăng nhập.
- **client.c:109–119, 304–340** — client hiển thị menu phù hợp trạng thái đăng nhập và gửi LOGOUT khi người dùng chọn.

**Kiểm chứng:** **tests/test_integration.py:118–128** chứng minh LIST trước LOGIN bị từ chối và sau LOGIN hoạt động; **:228–231** kiểm tra logout/login lại; **:238–245** xác thực lại sau khi khởi động server.

**Ranh giới phiên:** phiên gắn với kết nối, không có token đăng nhập lâu dài. Sau khi ngắt kết nối phải đăng nhập lại; dữ liệu phòng/bài làm vẫn nằm trong SQLite.

### 5. Kiểm soát quyền truy cập — 1 điểm

**Yêu cầu:** ngăn người dùng thực hiện thao tác không thuộc quyền của họ.

**Vị trí code và quy tắc:**

| Quy tắc | Vị trí kiểm tra |
|---|---|
| Mọi lệnh nghiệp vụ yêu cầu đăng nhập | server.c:266–267 |
| Chỉ admin thêm câu hỏi | server.c:293–295 |
| Chỉ chủ phòng bắt đầu phòng | server.c:356–361 và hàm owner tại :116–120 |
| Chỉ thành viên phòng được nhận đề | server.c:371–375 và hàm member tại :110–114 |
| Chỉ thành viên phòng được lưu đáp án/nộp bài | server.c:388–393, 403–407 |
| Chỉ chủ phòng hoặc thành viên xem kết quả, và chỉ khi phòng kết thúc | server.c:444–451 |
| Chỉ chủ bài luyện tập được xem/trả lời/nộp bài của mình | server.c:476–527, các truy vấn theo p.user_id |

**Điểm dễ nhầm:** vai trò admin và quyền chủ phòng là hai việc khác nhau. Tài khoản student cũng được CREATE phòng; admin không tự nhiên có quyền START phòng do student tạo.

**Kiểm chứng:** **tests/test_integration.py:148** student ADDQ bị ERR|FORBIDDEN; **:153** người ngoài không lấy được đề; **:193–194** admin không thể bắt đầu phòng của student; **:152, 203** không xem RESULTS khi phòng chưa kết thúc.

### 6. Tham gia chế độ luyện tập — 1 điểm

**Yêu cầu:** chọn số câu và thời gian, làm bài cá nhân, nhận tổng số câu đúng.

**Vị trí code:**

- **server.c:97–99** — bảng practices, practice_questions và practice_answers.
- **server.c:460–476** — PRACTICE kiểm tra số câu/thời gian, đủ ngân hàng câu hỏi, tạo bài, chọn câu ngẫu nhiên và đặt end_at.
- **server.c:478–489** — PQUESTION trả một câu cùng bốn lựa chọn, không trả đáp án đúng.
- **server.c:491–503** — PANSWER lưu hoặc thay lựa chọn khi bài còn mở.
- **server.c:505–528** — PSUBMIT chấm điểm; PSTATUS trả trạng thái hoặc điểm đã lưu.
- **server.c:198–211, 532–540** — luồng nền tự chấm bài luyện tập hết giờ.
- **client.c:259–283** — hỏi số câu/thời gian, lấy câu, nhận câu trả lời và hiển thị PSCORE.

**Kiểm chứng:** **tests/test_integration.py:182–189** tạo, trả lời và nộp bài luyện tập; **:217–225** kiểm tra bài luyện tập hết giờ; **:94–99** xác nhận ngân hàng mẫu đủ để mở bài luyện tập 20 câu trên database mới.

**Giới hạn:** hết giờ luyện tập được lưu điểm trên server, nhưng không có EVENT chủ động như phòng thi; client lấy điểm qua PSUBMIT/PSTATUS.

### 7. Tạo phòng thi — 2 điểm

**Yêu cầu:** người dùng tạo phòng, đặt số câu và thời lượng.

**Vị trí code:**

- **server.c:92–93** — bảng rooms lưu owner_id, count, duration, state, start_at/end_at; room_questions cố định bộ câu đã chọn.
- **server.c:305–324** — CREATE kiểm tra tên phòng, số câu 1–50, thời gian 10–86.400 giây, đủ số câu hỏi; dùng transaction để tạo phòng và gắn câu hỏi ngẫu nhiên; trả OK|ROOM|WAITING.
- **server.c:122–125** — question_count đếm câu hỏi sẵn có.
- **seed_questions.c:9–63, server.c:101** — 20 câu hỏi mẫu được nạp khi ngân hàng đang rỗng, đủ để tạo phòng demo ngay.
- **client.c:135–143** — màn hình nhập tên phòng, số câu và thời lượng.

**Điểm dữ liệu quan trọng:** câu được chọn lúc CREATE và lưu trong room_questions. Những lần EXAM sau đó đọc cùng bộ câu, không bốc ngẫu nhiên lại. CREATE chỉ mở phòng; đồng hồ thi chưa chạy cho đến START.

**Kiểm chứng:** **tests/test_integration.py:135–136** database cố ý để rỗng thì CREATE bị từ chối; **:149** tạo phòng sau khi thêm câu; **:94–99** database mới có 20 câu mẫu và CREATE phòng 20 câu thành công; **:103–106** khởi động lại không tạo câu mẫu trùng.

**Giới hạn:** chưa có tham số chọn câu theo topic/difficulty khi tạo phòng. Hai trường này được lưu trong bảng questions, nhưng CREATE hiện chọn ngẫu nhiên từ toàn bộ ngân hàng.

### 8. Xem danh sách phòng thi — 1 điểm

**Yêu cầu:** xem phòng và trạng thái chưa bắt đầu/đang diễn ra/đã kết thúc.

**Vị trí code:**

- **server.c:92** — state chỉ có WAITING, RUNNING, FINISHED.
- **server.c:326–333** — LIST truy vấn 100 phòng mới nhất, trả tên, state, số câu, thời lượng, tên chủ phòng.
- **client.c:121–134** — định dạng danh sách thành bảng để người dùng xem.

**Kiểm chứng:** **tests/test_integration.py:150, 163, 178** kiểm tra lần lượt trạng thái WAITING, RUNNING và FINISHED trong phản hồi LIST.

**Giới hạn:** danh sách hiện lấy tối đa 100 phòng, chưa có phân trang hay tìm kiếm.

### 9. Tham gia vào phòng thi — 2 điểm

**Yêu cầu:** vào phòng khi bài thi chưa bắt đầu.

**Vị trí code:**

- **server.c:94** — enrollments lưu cặp room_id/user_id và thời điểm tham gia.
- **server.c:335–354** — JOIN xác nhận phòng tồn tại, state là WAITING, người dùng chưa tham gia, chưa đủ MAX_USERS=32; sau đó ghi enrollments.
- **server.c:110–114** — hàm member kiểm tra người dùng đã ở trong phòng.
- **client.c:243–258** — sau JOIN thành công, client tự gửi WAIT để chờ chủ phòng.

**Kiểm chứng:** **tests/test_integration.py:151** JOIN khi WAITING thành công; **:162** JOIN sau START bị ERR|ROOM_NOT_WAITING; **:198–205** hai người cùng vào một phòng và nhận kết quả riêng.

**Điểm cần nói khi trình bày:** chủ phòng không tự động là thí sinh. Muốn thi, một tài khoản phải JOIN phòng khi còn WAITING.

### 10. Bắt đầu bài thi — 1 điểm

**Yêu cầu:** chủ phòng chủ động bắt đầu; server phát đề cho người tham gia.

**Vị trí code:**

- **server.c:356–369** — START chỉ cho chủ phòng, cần ít nhất một thí sinh, chuyển WAITING thành RUNNING và đặt end_at bằng thời gian server cộng duration.
- **server.c:371–386** — EXAM/WAIT kiểm tra tư cách thành viên; khi RUNNING, gọi exam_payload.
- **server.c:215–231** — exam_payload tạo thông điệp EXAM với thời hạn chung, toàn bộ câu hỏi và đáp án người dùng đã chọn; không có cột đáp án đúng trong truy vấn gửi cho client.
- **server.c:553–566** — WAIT long polling, kiểm tra lặp khoảng tối đa 30 giây; sau đó client có thể gửi WAIT tiếp.
- **client.c:243–258** — người thi chờ đến khi nhận EXAM; **client.c:191–242** chuyển sang màn hình làm bài.

**Kiểm chứng:** **tests/test_integration.py:155–161** một client gửi WAIT, chủ phòng gửi START, người chờ nhận EXAM với đúng hai câu; **:193–194** sai chủ phòng bị từ chối.

**Giới hạn:** bản tiếng Việt yêu cầu người dùng quyết định lúc bắt đầu; code chưa có nút kết thúc phòng thủ công trước hạn như phần mô tả mở rộng của bản tiếng Anh.

### 11. Nộp bài — 3 điểm

Đây là tiêu chí lớn nhất và gồm cả lưu đáp án, sửa đáp án, nộp sớm, chấm đúng và tự chấm khi hết giờ.

**Vị trí code:**

1. **Lưu/sửa đáp án:** **server.c:388–401** xử lý ANSWER; chỉ nhận A/B/C/D, kiểm tra thành viên, phòng đang RUNNING, còn thời gian và chưa có kết quả. Lệnh INSERT ... ON CONFLICT DO UPDATE ghi đè lựa chọn cũ cho cùng câu. Bảng exam_answers ở **server.c:95**.
2. **Nộp sớm:** **server.c:403–424** xử lý SUBMIT; nếu đã có điểm thì trả điểm cũ. Nếu còn thi, gọi room_score và submit_user, trả SCORE|đúng|tổng.
3. **Chấm:** **server.c:127–145** room_score đối chiếu exam_answers.selected với questions.correct; submit_user ghi kết quả bằng INSERT OR IGNORE, kèm thời điểm và cờ automatic. Không chấm lặp khi gửi SUBMIT thêm lần nữa.
4. **Kết thúc phòng khi mọi người đã nộp:** **server.c:160–170** đếm người tham gia chưa có kết quả; nếu không còn, chuyển phòng sang FINISHED.
5. **Hết giờ:** **server.c:172–197** tìm phòng quá end_at, chấm người chưa nộp, chuyển FINISHED; **:532–540** luồng monitor chạy kiểm tra khoảng mỗi 200 ms.
6. **Thông báo tự động:** **server.c:147–159** gửi EVENT|AUTO_SCORE cho các kết nối còn hoạt động của người đó. **client.c:25–48** có luồng nhận riêng để hiển thị sự kiện ngay cả khi người dùng đang ở lời nhắc nhập.
7. **Lấy lại điểm:** **server.c:426–441** STATUS trả SCORE đã lưu; người dùng kết nối lại có thể đăng nhập và lấy điểm. **client.c:191–242** hiển thị thời gian còn lại, chọn câu, đổi đáp án và xác nhận nộp.

**Kiểm chứng:** **tests/test_integration.py:168–169** trả lời sai rồi đổi sang đúng; **:170–176** lấy lại đáp án đã lưu từ kết nối khác; **:177–180** nộp sớm, điểm đúng, gọi SUBMIT lần hai không đổi điểm; **:207–227** đợi hết giờ, nhận EVENT trên socket và trên client thật, STATUS trả điểm, ANSWER sau giờ bị từ chối.

**Giới hạn kỹ thuật:** đồng hồ server quyết định hạn nộp. Nếu người dùng mất kết nối đúng lúc hết giờ, sự kiện push không tới kết nối đã đóng, nhưng điểm vẫn lưu trong SQLite và có thể lấy lại bằng STATUS sau khi đăng nhập lại.

### 12. Xem kết quả thi của các phòng thi — 1 điểm

**Yêu cầu:** xem kết quả sau khi phòng thi kết thúc.

**Vị trí code:**

- **server.c:96** — bảng results lưu room_id, user_id, score, total, submitted_at và automatic.
- **server.c:444–458** — RESULTS kiểm tra người hỏi là chủ phòng hoặc thí sinh; phòng phải FINISHED; truy vấn điểm từng người, sắp xếp điểm giảm dần rồi tên.
- **client.c:150–166** — hiển thị User, Correct, Total và Auto submitted.
- **server.c:426–441** — STATUS cho cá nhân lấy điểm đã lưu ngay cả khi vừa kết nối lại.

**Kiểm chứng:** **tests/test_integration.py:152, 203** kết quả trước khi phòng FINISHED bị từ chối; **:179, 205, 226** kết quả sau khi hoàn thành đúng cả trường hợp tự nộp; **:238–245** kết quả vẫn còn sau khi restart server.

**Phạm vi:** đây là kết quả của **một phòng được chỉ định**, không phải dashboard thống kê mọi bài từng làm hay biểu đồ lịch sử cá nhân.

### 13. Ghi log hoạt động — 1 điểm

**Yêu cầu:** lưu dấu các hành động quan trọng của hệ thống.

**Vị trí code:**

- **server.c:100** — bảng logs có id, user_id, event, detail và at; at mặc định là Unix timestamp lúc ghi.
- **server.c:53–60** — log_event ghi một dòng vào SQLite.
- **server.c:251, 263–264, 270, 289** — đăng ký, đăng nhập thành công/thất bại, đăng xuất, đổi mật khẩu.
- **server.c:302, 323, 353, 368** — thêm câu hỏi, tạo phòng, tham gia phòng, bắt đầu phòng.
- **server.c:143, 168, 195** — nộp bài thủ công/tự động, phòng kết thúc vì mọi người đã nộp hoặc hết giờ.
- **server.c:475, 209, 522** — bắt đầu/nộp/tự nộp bài luyện tập.
- **server.c:576–588, 597** — ngắt kết nối và server khởi động.

**Kiểm chứng:** **tests/test_integration.py:236** truy vấn bảng logs để xác nhận ít nhất ba sự kiện START_ROOM. Các sự kiện khác có lời gọi log_event trong code, nhưng bộ kiểm thử hiện chưa khẳng định riêng số dòng cho từng loại log.

**Giới hạn:** log đang ghi vào SQLite, không có màn hình client để tra cứu log và không ghi địa chỉ IP của client. Đề tài tiếng Việt chỉ nêu “ghi log hoạt động”, không đòi giao diện quản trị log hay IP.

## Kiểm thử đã thực hiện và ý nghĩa

**Biên dịch Windows:** build-windows.ps1 gọi GCC với -std=c11 và tạo server.exe, client.exe. Lần biên dịch sau khi thêm 20 câu mẫu đã thành công.

**Kiểm thử tích hợp:** chạy tests/test_integration.py với server.exe/client.exe thật trên Windows ngày 11/11/2026; kết quả: **All integration checks passed.** File test gồm hai luồng:

- **test_starter_bank, dòng 87–108:** database mới có 20 câu, tạo phòng và bài luyện tập 20 câu được, khởi động lại không thêm trùng.
- **test_full_flow, dòng 111–247:** khung TCP bị chia/gộp, tài khoản, quyền, phòng thi, chờ phát đề, đổi đáp án, nộp sớm, hết giờ, luyện tập, đổi mật khẩu, log và dữ liệu sau restart.

**Ý nghĩa giới hạn:** một lượt kiểm thử tích hợp thành công chứng minh các tình huống được mô tả trong test hoạt động ở môi trường Windows hiện tại. Chưa có số đo tải đồng thời lớn, benchmark tốc độ/độ trễ, kiểm thử trên máy ảo của giảng viên hoặc kiểm thử giao diện đồ họa.

## Những điểm ngoài hoặc sát ranh giới tiêu chí

1. **GUI:** ứng dụng là giao diện dòng lệnh trong client.c. GUI không thuộc 13 tiêu chí của file tiếng Việt; file tiếng Anh có nêu GUI riêng.
2. **Lọc câu theo topic/difficulty:** bảng questions có hai cột này và ADDQ lưu được giá trị, nhưng CREATE/PRACTICE hiện chọn từ toàn ngân hàng, chưa có lựa chọn bộ lọc khi tạo bài. Đây là phần mở rộng của bản tiếng Anh.
3. **Bảo mật mạng:** mật khẩu được băm khi lưu, nhưng đường truyền TCP không có TLS. Nên demo trên mạng tin cậy; cần TLS nếu triển khai ngoài môi trường học tập.
4. **Câu hỏi mẫu:** 20 câu tiếng Anh được nhúng trong seed_questions.c để có thể thi ngay với database mới. Muốn dùng ngân hàng riêng, đặt EXAM_SKIP_SEED=1 trước lần chạy đầu rồi thêm câu qua admin hoặc dùng tools/import_questions.py.
5. **Nền tảng:** source biên dịch theo C11 nhưng dùng SQLite, pthread và API mạng của hệ điều hành. Windows cần MinGW và thư viện theo build-windows.ps1; Linux cần GCC, SQLite development headers, OpenSSL và pthread.

## Kết luận

Bản **NetProgramming-complete** có mã nguồn thực hiện **13/13 tiêu chí chức năng** của đề tài 10 tiếng Việt, với tổng trọng số **20 điểm tối đa theo bảng đề**. Kết luận này dựa trên các vị trí code nêu trên và một lượt kiểm thử tích hợp thành công trên Windows. **Không nên tự ghi “đạt chắc chắn 20/20”**: điểm phụ thuộc vào việc chạy trên máy chấm, dữ liệu demo, yêu cầu giảng viên diễn giải từng tiêu chí và chất lượng vận hành thực tế.
