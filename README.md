# Online Test System — đề tài 10

Đây là **bản project mới**, độc lập với `NetProgramming-main`. Bản gốc không bị sửa. Ứng dụng vẫn dùng **C, TCP socket, pthread và SQLite**, nhưng vòng đời phòng thi, giao thức truyền dữ liệu và chế độ luyện tập đã được viết lại theo đề tài số 10 trong `IT4062_Network_Programing_Project_Topic_TungBT.xlsx`.

## Luồng hoạt động

1. Server mở `exam.db`, tạo bảng nếu đây là lần chạy đầu và nạp **20 câu hỏi mẫu** khi ngân hàng câu hỏi còn rỗng. Server lắng nghe cổng 9000 và tạo một luồng cho mỗi client.
2. Người dùng đăng ký, đăng nhập. Tài khoản nào cũng có thể tạo phòng; chỉ **chủ phòng** được bắt đầu phòng. Admin được thêm câu hỏi vào ngân hàng.
3. Chủ phòng tạo phòng với số câu và thời lượng. Server chỉ tạo nếu ngân hàng có đủ câu; câu hỏi được chọn ngẫu nhiên và cố định cho phòng. Trạng thái đầu tiên là `WAITING`.
4. Người thi `JOIN` khi phòng còn `WAITING`. Client gửi `WAIT` và chờ. Khi chủ phòng gửi `START`, server đặt một `end_at` chung và trả **toàn bộ đề** cho các client đang chờ. Trạng thái chuyển thành `RUNNING`.
5. Người thi gửi `ANSWER` cho từng câu, có thể đổi đáp án. Server lưu lựa chọn trong SQLite. `SUBMIT` chấm ngay và trả `SCORE`.
6. Khi hết giờ, luồng nền của server tự chấm những người chưa nộp và **chủ động gửi `EVENT|AUTO_SCORE`** tới các kết nối của người đó. Client có luồng nhận riêng nên hiển thị điểm ngay cả khi người dùng đang ở lời nhắc nhập. Sau khi kết nối lại, `STATUS` trả điểm đã lưu. Khi tất cả đã nộp hoặc hết giờ, phòng chuyển thành `FINISHED`; lúc đó chủ phòng và người tham gia mới xem được `RESULTS`.
7. Chế độ `PRACTICE` tạo một bài riêng với số câu và thời lượng do người dùng chọn. Server giữ đáp án đúng; `PSUBMIT` hoặc hết giờ trả tổng số câu đúng.

Mọi trạng thái quan trọng nằm trong SQLite, nên sau khi khởi động lại server vẫn xem được phòng và kết quả; bài đang chạy cũng được kết thúc đúng hạn khi server hoạt động lại.

## Đối chiếu 13 tiêu chí Excel (20 điểm)

| Tiêu chí | Cách thực hiện trong bản này |
|---|---|
| Xử lý truyền dòng (1) | TCP dùng khung **4 byte độ dài theo network byte order + nội dung UTF-8**. `send_all`/`recv_all` xử lý chia/gộp gói tin trong `net.c`. |
| Vào/ra socket trên server (2) | `socket` → `bind` → `listen` → `accept`; kiểm tra lỗi khởi động, một pthread cho mỗi kết nối trong `server.c`. |
| Đăng ký và quản lý tài khoản (2) | `REGISTER`, `CHPASS`, `LOGOUT`; tài khoản lưu SQLite, mật khẩu lưu PBKDF2-HMAC-SHA256 với salt riêng trong `password.c`. |
| Đăng nhập và quản lý phiên (2) | `LOGIN`, thông tin người dùng gắn với kết nối; ngắt kết nối hoặc `LOGOUT` xóa phiên. |
| Kiểm soát quyền truy cập (1) | Admin mới được `ADDQ`; chủ phòng mới được `START`; thành viên mới được nhận đề/nộp; chủ phòng hoặc thành viên mới xem kết quả phòng đã kết thúc. |
| Tham gia chế độ luyện tập (1) | `PRACTICE`, `PQUESTION`, `PANSWER`, `PSUBMIT`, `PSTATUS`; chọn số câu, thời gian, chấm tổng câu đúng khi nộp hoặc hết giờ. |
| Tạo phòng thi (2) | `CREATE|tên|số_câu|thời_lượng`; kiểm tra đủ câu hỏi, tạo `WAITING`, chọn và cố định đề cho phòng. |
| Xem danh sách phòng thi (1) | `LIST` trả tên, trạng thái `WAITING/RUNNING/FINISHED`, số câu, thời lượng, chủ phòng. |
| Tham gia vào phòng thi (2) | `JOIN` chỉ thành công khi `WAITING`; kiểm tra đăng nhập và sức chứa. |
| Bắt đầu bài thi (1) | Chủ phòng gửi `START`; server đặt hạn chung, trả toàn bộ đề qua yêu cầu `WAIT` đang chờ. |
| Nộp bài (3) | `ANSWER` lưu/thay đáp án; `SUBMIT` chấm sớm; server tự chấm và gửi `EVENT|AUTO_SCORE` khi hết giờ; `STATUS` và `SUBMIT` trả điểm đã lưu mà không chấm lặp. |
| Xem kết quả thi của các phòng thi (1) | `RESULTS` chỉ khi `FINISHED`; có điểm từng người và cờ nộp tự động. Dữ liệu tồn tại sau khi restart. |
| Ghi log hoạt động (1) | Bảng `logs` ghi đăng ký, đăng nhập, tạo/vào/bắt đầu/kết thúc phòng, nộp bài, luyện tập, đổi mật khẩu và ngắt kết nối. |

## Biên dịch

### Windows

Cần MinGW GCC, thư viện SQLite trong `C:\Program Files\mingw64\opt` như máy đang dùng. Chạy trong PowerShell:

```powershell
cd 'D:\tài liệu\network programming\Project\NetProgramming-complete'
.\build-windows.ps1
```

Script tạo `server.exe` và `client.exe`. Nếu MinGW hoặc SQLite nằm ở chỗ khác, sửa hai biến đường dẫn đầu `build-windows.ps1`.

### Linux

Cần GCC, `make`, SQLite development headers, OpenSSL development headers và pthread:

```sh
make
```

## Chạy và thử bằng giao diện

Mở ít nhất ba terminal:

```powershell
# Terminal 1
$env:EXAM_ADMIN_CODE='ma-bi-mat-cua-nhom'
.\server.exe 9000 exam.db

# Terminal 2: người tạo phòng
.\client.exe 127.0.0.1 9000

# Terminal 3: người thi
.\client.exe 127.0.0.1 9000
```

Trong terminal 2, đăng ký và đăng nhập, rồi chọn **Create room**. Database mới đã có sẵn 20 câu hỏi mẫu về C, mạng, cơ sở dữ liệu và bảo mật; có thể tạo phòng hoặc luyện tập ngay. Nếu muốn tự thêm câu hỏi, đăng ký admin bằng mã đã đặt rồi chọn **Add question**. Tài khoản student cũng có quyền tạo phòng, nhưng không có quyền thêm câu hỏi. Trong terminal 3, đăng ký và đăng nhập, chọn **Join room**. Client sẽ chờ. Quay về terminal 2, chọn **Start room**; terminal 3 nhận đề và bắt đầu làm. Sau khi nộp hoặc hết giờ, dùng **Results** để xem điểm.

Câu hỏi mẫu chỉ được nạp khi bảng `questions` đang rỗng; khởi động lại server không tạo thêm bản sao. Muốn dùng ngân hàng tự chuẩn bị ngay từ đầu, đặt `EXAM_SKIP_SEED=1` trước khi chạy server, sau đó thêm câu qua admin hoặc dùng công cụ nhập từ project cũ.

`EXAM_ADMIN_CODE` mặc định là `admin123` nếu không đặt biến môi trường; nên đặt mã khác khi demo. Server mặc định dùng cổng `9000` và `exam.db`. Client mặc định kết nối `127.0.0.1:9000`.

## Giao thức TCP

Mỗi thông điệp: `uint32` big-endian ghi số byte nội dung, tiếp theo là đúng số byte nội dung. Lệnh và tham số trong nội dung cách nhau bằng `|`. Trường người dùng không được có `|`, `\r`, `\n`. Phản hồi bắt đầu bằng `OK`, `ERR`, `ROOMS`, `EXAM`, `SCORE`, `RESULTS`, `PRACTICE`, `Q`, `PSCORE`, `PSTATUS` hoặc `FINISHED`. `EVENT|AUTO_SCORE` là thông báo tự động của server, có thể đến giữa hai phản hồi lệnh; client phân biệt nó bằng luồng nhận riêng. `WAIT` là long polling: sau tối đa 30 giây nếu chủ phòng chưa bắt đầu, server trả `WAITING` và client gửi lại.

Các lệnh: `REGISTER`, `LOGIN`, `LOGOUT`, `CHPASS`, `ADDQ`, `CREATE`, `LIST`, `JOIN`, `START`, `WAIT`, `EXAM`, `ANSWER`, `SUBMIT`, `STATUS`, `RESULTS`, `PRACTICE`, `PQUESTION`, `PANSWER`, `PSUBMIT`, `PSTATUS`. Client chỉ hiển thị câu hỏi và các lựa chọn; đáp án đúng ở server.

## Kiểm thử

Sau khi biên dịch, chạy bài kiểm thử tích hợp:

```powershell
python tests/test_integration.py
```

Bài kiểm thử dùng database tạm, khởi động **server thật** và gửi thông điệp TCP như client. Nó kiểm tra câu hỏi mẫu khi khởi tạo database, khung bị chia nhỏ và nhiều khung trong một lần gửi, đăng ký/đăng nhập, phân quyền, tạo/vào/bắt đầu phòng, phát đề, đổi đáp án, nộp sớm, thông báo tự động khi hết giờ (cả trên client thật), luyện tập, đổi mật khẩu và dữ liệu sau restart. Phần kiểm thử luồng thi riêng dùng `EXAM_SKIP_SEED=1` để có ngân hàng câu hỏi xác định.

## Dữ liệu từ project cũ

Nếu muốn lấy ngân hàng câu hỏi từ `NetProgramming-main/test_system.db`, chạy server mới một lần để tạo schema rồi **dừng server**. Sau đó chạy:

```powershell
python tools/import_questions.py ..\NetProgramming-main\test_system.db exam.db
```

Script chỉ lấy câu hỏi hợp lệ và bỏ qua câu trùng. Nó có thể thêm câu hỏi từ project cũ bên cạnh 20 câu mẫu; nếu chỉ muốn dùng câu hỏi cũ, khởi động server lần đầu với `EXAM_SKIP_SEED=1`. Script không chuyển tài khoản, phòng và lượt thi cũ vì cấu trúc vòng đời và mật khẩu của bản mới khác bản cũ.

## Giới hạn triển khai

Đây là ứng dụng phục vụ bài tập môn học. TCP hiện chưa mã hóa bằng TLS; chỉ nên chạy trong mạng lab hoặc mạng tin cậy. Thời gian thi tối thiểu 10 giây, tối đa 24 giờ; mỗi phòng tối đa 50 câu và 32 người. Người thi làm bài trên client dòng lệnh; đồng hồ server quyết định hạn nộp, không phụ thuộc đồng hồ client.
