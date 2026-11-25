# Online Test System — đề tài 10

Ứng dụng thi trắc nghiệm trực tuyến được xây dựng bằng **C11, TCP socket, pthread và SQLite**. Hệ thống hỗ trợ đăng ký, đăng nhập, luyện tập cá nhân và thi theo phòng có giới hạn thời gian. Server quản lý câu hỏi, bài làm và kết quả; client cung cấp giao diện dòng lệnh cho người dùng.

## Luồng hoạt động

Xem [session.md](session.md) để theo dõi chi tiết từng giai đoạn, thông điệp trao đổi, trạng thái và nơi lưu dữ liệu.

1. Server mở `question.db`, tạo bảng nếu đây là lần chạy đầu và nạp **20 câu hỏi mẫu** khi ngân hàng câu hỏi còn rỗng. Server lắng nghe cổng 9000 và tạo một luồng cho mỗi client.
2. Người dùng đăng ký, đăng nhập. Tài khoản nào cũng có thể tạo phòng; chỉ **chủ phòng** được bắt đầu phòng. Ngân hàng câu hỏi được chuẩn bị trong seed_questions.c.
3. Chủ phòng tạo phòng với số câu và thời lượng. Server chỉ tạo nếu ngân hàng có đủ câu; câu hỏi được chọn ngẫu nhiên và cố định cho phòng. Trạng thái đầu tiên là `WAITING`.
4. Người thi `JOIN ROOM` khi phòng còn `WAITING`. Client gửi `AWAIT ROOM` và chờ. Khi chủ phòng gửi `START ROOM`, server đặt một `end_at` chung và trả **toàn bộ đề** cho các client đang chờ. Trạng thái chuyển thành `RUNNING`.
5. Người thi gửi `ANSWER ROOM` cho từng câu, có thể đổi đáp án. Server lưu lựa chọn trong SQLite. `SUBMIT ROOM` chấm ngay và trả `127`.
6. Khi hết giờ, luồng nền của server tự chấm những người chưa nộp và **chủ động gửi `210`** tới các kết nối của người đó. Client có luồng nhận riêng nên hiển thị điểm ngay cả khi người dùng đang ở lời nhắc nhập. Sau khi kết nối lại, `STATUS ROOM` trả điểm đã lưu. Khi tất cả đã nộp hoặc hết giờ, phòng chuyển thành `FINISHED`; lúc đó chủ phòng và người tham gia mới xem được `RESULTS ROOM`.
7. Chế độ `CREATE PRACTICE` tạo một bài riêng với số câu và thời lượng do người dùng chọn. Server giữ đáp án đúng; `SUBMIT PRACTICE` hoặc hết giờ trả tổng số câu đúng.

Mọi trạng thái quan trọng nằm trong SQLite, nên sau khi khởi động lại server vẫn xem được phòng và kết quả; bài đang chạy cũng được kết thúc đúng hạn khi server hoạt động lại.

## Giao thức và phạm vi chức năng

Giao thức mới là dòng văn bản UTF-8 kết thúc CRLF, tham số cách nhau bằng dấu cách; phản hồi bằng mã số. Xem [protocol.md](protocol.md) để biết danh mục đầy đủ. Ví dụ `CREATE ROOM PhongA 2 300`, `JOIN ROOM PhongA`, `START ROOM PhongA`, `ANSWER ROOM PhongA 1 B`. Luyện tập dùng cùng nhóm hành động: `CREATE PRACTICE`, `QUESTIONS PRACTICE`, `ANSWER PRACTICE`, `SUBMIT PRACTICE`. Không còn thêm câu hỏi hoặc đổi mật khẩu qua client/server.

Ngân hàng gồm 20 câu tiếng Việt trong seed_questions.c. SQLite vẫn giữ cấu trúc dữ liệu cũ, nên không cần xóa question.db khi đổi giao thức. Phải biên dịch lại cả client và server. Các chuỗi có dấu | còn trong mã là biểu diễn nội bộ ở giữa codec và nghiệp vụ; trên socket không có dấu | hoặc tiền tố độ dài.

## Biên dịch

### Windows

Cần MinGW GCC và thư viện SQLite. Mở PowerShell tại thư mục project rồi chạy:

```powershell
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
.\server.exe 9000 question.db

# Terminal 2: người tạo phòng
.\client.exe 127.0.0.1 9000

# Terminal 3: người thi
.\client.exe 127.0.0.1 9000
```

Trong terminal 2, đăng ký và đăng nhập, rồi chọn **Create room**. Khi khởi tạo database rỗng, server nạp 20 câu hỏi mẫu **bằng tiếng Việt** về toán học, con vật, cây cối và đời sống; có thể tạo phòng hoặc luyện tập ngay. Client Windows đặt bảng mã console UTF-8 để hiển thị dấu tiếng Việt. Mọi tài khoản đều có quyền tạo phòng; chỉ chủ phòng được bắt đầu phòng. Trong terminal 3, đăng ký và đăng nhập, chọn **Join room**. Client sẽ chờ. Quay về terminal 2, chọn **Start room**; terminal 3 nhận đề và bắt đầu làm. Sau khi nộp hoặc hết giờ, dùng **Results** để xem điểm.

Câu hỏi mẫu chỉ được nạp khi bảng `questions` đang rỗng; khởi động lại server không tạo thêm bản sao. File `question.db` dùng cho bộ 20 câu tiếng Việt mới và được tạo tự động nếu chưa có. Các database demo cũ không bị thay đổi; nếu truyền tên file database khác khi chạy server, chương trình sẽ dùng nội dung của file đó. Muốn dùng ngân hàng câu hỏi tự chuẩn bị, đặt `EXAM_SKIP_SEED=1` trước khi chạy server lần đầu, sau đó chuẩn bị dữ liệu SQLite ngoài ứng dụng.

Server mặc định dùng cổng `9000` và `question.db`. Client mặc định kết nối `127.0.0.1:9000`.

## Kiểm thử

Sau khi biên dịch, chạy `python tests/test_integration.py`. Kiểm thử dùng database tạm và giao thức CRLF trực tiếp, không tác động question.db đang dùng.

## Giới hạn

Chưa có TLS, chống thử mật khẩu liên tục hoặc phiên bản giao thức. Mỗi phòng tối đa 32 người, đề tối đa 50 câu nhưng phải đủ câu trong ngân hàng. Server dùng một khóa SQLite chung, AWAIT ROOM thăm dò mỗi 100 ms. Đây là ứng dụng phục vụ môn học trong mạng tin cậy.

## Tài khoản thống nhất và tương thích database

Ứng dụng đã bỏ vai trò admin/student và EXAM_ADMIN_CODE khỏi đăng ký, đăng nhập, menu và kiểm tra quyền. REGISTER chỉ nhận username password, LOGIN trả 111 không có tham số role. Quyền phụ thuộc người tạo phòng, thành viên phòng hoặc chủ bài cá nhân. Cột users.role được giữ như dữ liệu tương thích với database cũ; tài khoản mới ghi giá trị legacy student để đáp ứng ràng buộc cũ, không dùng cột này để phân quyền. Tài khoản admin cũ vẫn đăng nhập được như mọi tài khoản. Không xóa hoặc ghi đè question.db.
