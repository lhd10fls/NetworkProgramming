# Đối chiếu tiêu chí sau khi sửa giao thức

Tài liệu này thay mô tả phiên bản giao thức cũ. Chạy build-windows.ps1 và tests/test_integration.py để kiểm chứng bản hiện tại. Điểm thực tế do giảng viên quyết định.

| Tiêu chí Excel | Triển khai hiện tại |
|---|---|
| Xử lý truyền dòng (1) | net.c đọc đến CRLF, hỗ trợ UTF-8, quoting, ROW; không có length prefix |
| Socket server (2) | server.c: một pthread/client, khóa gửi, SQLite chung |
| Đăng ký và quản lý tài khoản (2) | REGISTER/LOGIN/LOGOUT; PBKDF2. Đã bỏ đổi mật khẩu theo yêu cầu mới, cần xác nhận phạm vi tiêu chí với giảng viên |
| Đăng nhập và phiên (2) | Một phiên/tài khoản trên server; giải phóng khi logout/disconnect |
| Kiểm soát quyền (1) | Chỉ chủ phòng START; chỉ thành viên làm bài; bài cá nhân chỉ chủ sở hữu |
| Luyện tập (1) | CREATE/QUESTIONS/ANSWER/SUBMIT/STATUS với phạm vi PRACTICE |
| Tạo phòng (2) | CREATE ROOM; chọn và cố định đề SQLite |
| Danh sách phòng (1) | LIST ROOMS, phản hồi 121 |
| Tham gia phòng (2) | JOIN ROOM chỉ khi WAITING, tối đa 32 người |
| Bắt đầu (1) | START ROOM; AWAIT ROOM nhận 125 đề |
| Nộp bài (3) | SUBMIT ROOM trả 127; hết giờ tự chấm và gửi 210 |
| Kết quả phòng (1) | RESULTS ROOM trả 129 khi FINISHED |
| Log (1) | Bảng logs trong question.db |

## Thay đổi cần biết

net.c có codec chuyển chuỗi trường nội bộ thành giao thức văn bản và ngược lại. Các chuỗi cũ dùng | trong server.c/client.c không phải dữ liệu truyền qua TCP. Phản hồi trên mạng dùng nhóm lỗi 400/401/403/404/409/500 thống nhất, theo sau là lý do; thành công 110–134, sự kiện 210. AWAIT là hành động; WAITING/RUNNING/FINISHED là trạng thái.

Đã bỏ ADDQ và CHPASS ở dispatch và menu. Ngân hàng câu hỏi chuẩn bị qua seed_questions.c. Mã lỗi lỗi SQLite có thể còn được gộp thành mã xung đột trong một số nhánh kế thừa, ví dụ lỗi INSERT tài khoản được báo USER_EXISTS; không nên hiểu mọi lỗi cơ sở dữ liệu đều trả 500 DB.

## Kiểm chứng

Bộ kiểm thử gửi trực tiếp thông điệp CRLF dạng văn bản, kiểm tra phân mảnh, gộp nhiều dòng, login, quyền, vòng đời phòng, sửa đáp án, nộp, tự chấm khi hết giờ, practice dùng lệnh chung và UI client thật với tiếng Việt. Database test tạm; không thay ngân hàng/tài khoản thực.

## Giới hạn

Chưa TLS hoặc giới hạn thử mật khẩu; PBKDF2 vẫn chạy dưới khóa SQLite chung. AWAIT dùng thăm dò 100 ms, một server không chia sẻ registry phiên với server khác. Cần đặt EXAM_ADMIN_CODE riêng. Cấu trúc SQLite và question.db được giữ tương thích.
