# Đặc tả giao thức ứng dụng Online Test System

Tài liệu này mô tả **giao thức ứng dụng do project thiết kế**, chạy trên **TCP/IPv4**. TCP chuyển luồng byte giữa hai máy; các quy tắc dưới đây xác định ranh giới thông điệp, cú pháp lệnh, ý nghĩa dữ liệu và trình tự trao đổi giữa client với server. Giao thức này không phải HTTP hay một giao thức Internet có tên đăng ký riêng.

## 1. Thành phần và phạm vi

| Thành phần | Vai trò |
|---|---|
| Server | Lắng nghe cổng TCP (mặc định 9000), xác thực, lưu SQLite, quyết định thời gian và chấm điểm. |
| Client | Kết nối đến IP/cổng server, gửi lệnh và hiển thị phản hồi hoặc sự kiện. |
| Kết nối | Một kết nối gắn với tối đa một phiên đăng nhập; mỗi tài khoản chỉ có một phiên đang hoạt động trên server. LOGOUT hoặc đóng socket giải phóng phiên. |
| Dữ liệu bền vững | Tài khoản, câu hỏi, phòng, đáp án và kết quả lưu trong SQLite; phiên TCP không được khôi phục sau khi ngắt kết nối. |

Nguồn code: **net.c/net.h** (đóng khung), **server.c** (xử lý lệnh và trạng thái), **client.c** (gửi yêu cầu và nhận phản hồi/sự kiện).

## 2. Khung thông điệp trên TCP

TCP là **luồng byte**, không giữ ranh giới hai lần gửi. Mỗi thông điệp ứng dụng có khung:

~~~text
Offset    Kích thước    Ý nghĩa
0         4 byte        L: độ dài nội dung, uint32 big-endian
4         L byte        Nội dung thông điệp
~~~

- **L tính bằng byte**, không phải số ký tự; nội dung tối đa **65.536 byte** (MAX_FRAME ở net.h:22).
- Phía gửi dùng **htonl**, phía nhận dùng **ntohl** để chuyển network byte order.
- **send_all/recv_all** lặp cho đến khi chuyển đủ 4 byte đầu và đủ L byte nội dung; không giả định một lệnh send/recv chuyển hết thông điệp.
- Nội dung là văn bản theo quy ước UTF-8. Code hiện xử lý chuỗi byte C và **chưa xác minh UTF-8 hợp lệ**. Không gửi byte NUL trong nội dung.
- Ví dụ LIST có L = 4: **00 00 00 04 4C 49 53 54** (hex).
- Nếu khung vượt giới hạn, nhận thiếu dữ liệu hoặc kết nối đóng, recv_frame trả thất bại và luồng xử lý kết thúc; không bảo đảm có phản hồi ERR.

**Vị trí code:** net.c:32–63; net.h:22–28. **Kiểm thử:** tests/test_integration.py:122 gửi khung thành từng byte; :138–142 gửi hai khung trong một lần ghi.

## 3. Cú pháp nội dung và quy tắc trao đổi

~~~text
Yêu cầu: COMMAND[|field1[|field2...]]
Phản hồi lỗi: ERR|CODE
Phản hồi thành công: một trong các định dạng ở mục 5 và 6
~~~

- Tên lệnh và mã trạng thái dùng chữ in hoa ASCII. Vai trò là **student/admin**; đáp án là **A/B/C/D**.
- **|** phân tách trường. Trường văn bản do người dùng nhập không được chứa **|**, CR, LF hoặc ký tự điều khiển; giao thức hiện không có cơ chế escape.
- Một số phản hồi chứa nhiều dòng bên trong **cùng một khung**. LF phân tách dòng bản ghi, không phân tách khung TCP.
- Số nguyên truyền bằng chữ số thập phân ASCII. **end_at** là Unix timestamp theo giây, do server đặt và đối chiếu với đồng hồ server.
- Khi trường cuối rỗng, vẫn giữ dấu phân cách. Ví dụ tài khoản student đăng ký bằng **REGISTER|alice|pw|student|**.
- Server xử lý các khung **tuần tự trên mỗi kết nối**. Giao thức hiện **không có request ID**; client gửi một yêu cầu, chờ phản hồi rồi mới gửi yêu cầu tiếp theo.
- Một yêu cầu có một phản hồi chính. **EVENT|AUTO_SCORE** là sự kiện bất đồng bộ, có thể đến giữa yêu cầu và phản hồi; client phải tách sự kiện này ra khi nhận.
- Lệnh **WAIT** có thể giữ yêu cầu tối đa khoảng 30 giây khi phòng chưa bắt đầu; sau đó server trả WAITING và client gửi WAIT lại.

**Vị trí code:** server.c:100–110, 260–561, 575–606; client.c:25–87.

## 4. Phiên và quyền truy cập

Trước khi đăng nhập, chỉ **REGISTER** và **LOGIN** được xử lý; lệnh nghiệp vụ khác trả **ERR|LOGIN_REQUIRED**. LOGIN thành công lưu user_id và role trong cấu trúc Client của server, không gửi token phiên. Nếu tài khoản đang đăng nhập trên một kết nối khác, LOGIN trả **ERR|ALREADY_LOGGED_IN** và kết nối mới vẫn chưa xác thực. Sau LOGOUT hoặc đóng kết nối cũ, tài khoản có thể đăng nhập lại.

| Hành động | Người được phép |
|---|---|
| ADDQ | Chỉ admin. |
| CREATE | Bất kỳ tài khoản đã đăng nhập; người tạo là chủ phòng. |
| START | Chỉ chủ phòng; admin không có quyền thay chủ phòng. |
| WAIT, EXAM, ANSWER, SUBMIT, STATUS | Chỉ người đã JOIN phòng. |
| RESULTS | Chủ phòng hoặc người đã JOIN, sau khi phòng FINISHED. |
| PQUESTION, PANSWER, PSUBMIT, PSTATUS | Chỉ chủ sở hữu bài luyện tập. |

**Vị trí code:** server.c:36–56, 280–304, 325–327, 367–407, 420–483, 510–560. **Kiểm thử phiên:** tests/test_integration.py:128–135 và :152–169.

## 5. Danh mục lệnh và phản hồi

Ký hiệu: **name** = tên phòng, **count** = số câu, **duration** = thời lượng giây, **pos** = số thứ tự câu bắt đầu từ 1, **choice** = A/B/C/D, **id** = mã bài luyện tập. Các mã lỗi liệt kê ở mục 8.

### 5.1. Tài khoản và câu hỏi

| Yêu cầu | Điều kiện/ý nghĩa | Phản hồi thành công |
|---|---|---|
| REGISTER\|username\|password\|role\|admin_code | Chưa đăng nhập; username tối đa 32, password tối đa 128 ký tự; role student/admin. Admin cần mã EXAM_ADMIN_CODE. Student để trống trường cuối. | OK\|REGISTERED |
| LOGIN\|username\|password | Chưa đăng nhập; xác minh mật khẩu và gắn phiên vào kết nối. Từ chối nếu tài khoản có phiên đang hoạt động. | OK\|LOGIN\|student hoặc OK\|LOGIN\|admin |
| LOGOUT | Đã đăng nhập; xóa phiên trên kết nối. | OK\|LOGOUT |
| CHPASS\|old_password\|new_password | Đã đăng nhập; mật khẩu cũ đúng, mật khẩu mới không rỗng và tối đa 128 ký tự. | OK\|PASSWORD_CHANGED |
| ADDQ\|text\|A\|B\|C\|D\|correct\|topic\|difficulty | Chỉ admin; text tối đa 200 ký tự, trường còn lại tối đa 100; correct là A/B/C/D. | OK\|QUESTION\|question_id |

### 5.2. Phòng thi

| Yêu cầu | Điều kiện/ý nghĩa | Phản hồi thành công |
|---|---|---|
| CREATE\|name\|count\|duration | Đã đăng nhập; name tối đa 48 ký tự, count 1–50, duration 10–86.400 giây, ngân hàng đủ câu. Server chọn và cố định bộ câu. | OK\|ROOM\|WAITING |
| LIST | Đã đăng nhập; lấy tối đa 100 phòng mới nhất. | ROOMS và các dòng R\|name\|state\|count\|duration\|owner |
| JOIN\|name | Phòng WAITING, chưa đủ 32 người; JOIN lại khi đã là thành viên vẫn được OK. | OK\|JOINED |
| START\|name | Đúng chủ phòng, phòng WAITING và có ít nhất một thành viên. Server đặt start_at/end_at chung. | OK\|STARTED |
| WAIT\|name | Chỉ thành viên; chờ tối đa khoảng 30 giây mỗi lần gọi. | WAITING, EXAM... hoặc FINISHED... |
| EXAM\|name | Chỉ thành viên; hỏi trạng thái/nhận lại đề ngay. | WAITING, EXAM... hoặc FINISHED... |
| ANSWER\|name\|pos\|choice | Thành viên, phòng RUNNING, còn thời gian, chưa nộp. Gửi lại cùng pos để đổi đáp án. | OK\|ANSWER_SAVED |
| SUBMIT\|name | Thành viên; nộp sớm khi còn giờ hoặc lấy điểm đã lưu nếu đã nộp. | SCORE\|correct\|total |
| STATUS\|name | Thành viên; xem trạng thái hoặc lấy lại điểm sau khi kết nối lại. | STATUS\|state\|end_at hoặc SCORE\|correct\|total |
| RESULTS\|name | Chủ phòng hoặc thành viên; phòng FINISHED. | RESULTS và các dòng R\|username\|correct\|total\|automatic |

### 5.3. Luyện tập

| Yêu cầu | Điều kiện/ý nghĩa | Phản hồi thành công |
|---|---|---|
| PRACTICE\|count\|duration | Đã đăng nhập; count 1–50, duration 10–86.400 giây, ngân hàng đủ câu. Bắt đầu tính giờ ngay. | PRACTICE\|id\|end_at\|count |
| PQUESTION\|id\|pos | Đúng chủ bài, bài còn thời gian, pos nằm trong bài. | Q\|pos\|text\|A\|B\|C\|D |
| PANSWER\|id\|pos\|choice | Đúng chủ bài, bài còn thời gian; gửi lại cùng pos để đổi đáp án. | OK\|ANSWER_SAVED |
| PSUBMIT\|id | Đúng chủ bài; nộp sớm hoặc lấy điểm đã lưu. | PSCORE\|correct\|total |
| PSTATUS\|id | Đúng chủ bài; xem bài còn chạy hay đã có điểm. | PSTATUS\|RUNNING\|end_at hoặc PSCORE\|correct\|total |

**Vị trí code:** server.c:264–561; client.c:99–305.

## 6. Phản hồi nhiều dòng và sự kiện

Các ví dụ trong mục này là **phần nội dung của khung**; trên TCP vẫn phải thêm 4 byte độ dài phía trước.

### 6.1. Danh sách phòng

~~~text
ROOMS
R|PhongA|WAITING|10|600|teacher
R|PhongB|FINISHED|5|120|alice
~~~

Mỗi dòng R gồm tên phòng, trạng thái, số câu, thời lượng giây và chủ phòng. Nếu chưa có phòng, nội dung chỉ là **ROOMS**.

### 6.2. Đề thi và bốn lựa chọn

~~~text
EXAM|PhongA|1794357565|2
Q|1|Which protocol is reliable?|UDP|TCP|IP|ARP|
Q|2|Which call accepts a client?|bind|accept|send|close|B
~~~

Mỗi dòng Q gồm **vị trí, nội dung câu hỏi, văn bản lựa chọn A, B, C, D và lựa chọn mà người thi đã lưu**. Trường cuối rỗng nếu chưa trả lời. **Server có gửi đầy đủ bốn lựa chọn A–D; server không gửi ký tự đáp án đúng**. Trường cuối B của câu thứ hai trong ví dụ chỉ là câu trả lời trước đó của chính người thi, không chứng minh B đúng. Khi vào bài thi, client hiện in ngay toàn bộ câu hỏi cùng bốn lựa chọn; chọn số câu sẽ hiện lại câu đó để trả lời.

end_at là Unix timestamp theo giây do server đặt; con số trong ví dụ chỉ minh họa định dạng. Phản hồi EXAM là **một khung**, các dòng Q được ngăn bằng LF trong nội dung.

### 6.3. Bảng điểm phòng

~~~text
RESULTS
R|alice|2|2|0
R|bob|1|2|1
~~~

Trường cuối **automatic** bằng 0 khi người thi nộp sớm, bằng 1 khi server tự nộp lúc hết giờ. Các dòng sắp theo điểm giảm dần rồi theo tên.

### 6.4. Thông báo tự động

~~~text
EVENT|AUTO_SCORE|PhongA|1|2
~~~

Sự kiện này được gửi tới kết nối còn hoạt động của người chưa nộp khi phòng hết giờ. Nó **không phải phản hồi của yêu cầu ngay trước đó**. Client cần nhận diện tiền tố EVENT|AUTO_SCORE|, hiển thị điểm và tiếp tục đợi phản hồi chính. Người đã mất kết nối có thể LOGIN lại rồi gửi STATUS để lấy điểm đã lưu. Luyện tập không có sự kiện tự động riêng; dùng PSTATUS hoặc PSUBMIT để lấy điểm.

**Vị trí code:** server.c:173–185, 198–237; client.c:25–48, 191–260.

## 7. Trạng thái và trình tự thông điệp

### 7.1. Trạng thái phiên

~~~text
CONNECTED_UNAUTH --REGISTER thành công--> CONNECTED_UNAUTH
CONNECTED_UNAUTH --LOGIN thành công--> AUTHENTICATED
CONNECTED_UNAUTH --LOGIN tài khoản đang hoạt động--> CONNECTED_UNAUTH
AUTHENTICATED --LOGOUT--> CONNECTED_UNAUTH
CONNECTED_UNAUTH/AUTHENTICATED --đóng socket--> DISCONNECTED
~~~

REGISTER tạo tài khoản nhưng không tự đăng nhập. LOGIN sai mật khẩu hoặc gặp phiên đang hoạt động không đổi trạng thái. Mỗi tài khoản chỉ được có một kết nối đang xác thực tại một thời điểm.

Ví dụ hai terminal cùng dùng tài khoản alice:

~~~text
Terminal 1 -> LOGIN|alice|pw       <- OK|LOGIN|student
Terminal 2 -> LOGIN|alice|pw       <- ERR|ALREADY_LOGGED_IN
Terminal 1 -> LOGOUT               <- OK|LOGOUT
Terminal 2 -> LOGIN|alice|pw       <- OK|LOGIN|student
~~~

### 7.2. Trạng thái phòng thi

~~~mermaid
stateDiagram-v2
    [*] --> WAITING: CREATE
    WAITING --> WAITING: JOIN
    WAITING --> RUNNING: START bởi chủ phòng
    RUNNING --> RUNNING: ANSWER / một số người SUBMIT
    RUNNING --> FINISHED: Mọi người đã nộp
    RUNNING --> FINISHED: Hết end_at
~~~

- **WAITING:** người dùng có thể JOIN; thành viên gọi WAIT/EXAM nhận WAITING.
- **RUNNING:** thành viên nhận EXAM, gửi ANSWER hoặc SUBMIT trước end_at. Có thể đổi đáp án bằng cách gửi ANSWER lại cùng vị trí.
- **FINISHED:** không nhận JOIN/ANSWER; chủ phòng và thành viên được xem RESULTS. Không có lệnh mở lại hoặc kết thúc thủ công trước giờ.
- Người tạo phòng **không tự động là thí sinh**; muốn làm bài thì JOIN khi phòng còn WAITING.

### 7.3. Trình tự một lượt thi

~~~mermaid
sequenceDiagram
    participant O as Chủ phòng
    participant S as Server
    participant P as Người thi
    O->>S: CREATE|PhongA|2|60
    S-->>O: OK|ROOM|WAITING
    P->>S: JOIN|PhongA
    S-->>P: OK|JOINED
    P->>S: WAIT|PhongA
    O->>S: START|PhongA
    S-->>O: OK|STARTED
    S-->>P: EXAM|PhongA|end_at|2 + 2 dòng Q
    P->>S: ANSWER|PhongA|1|B
    S-->>P: OK|ANSWER_SAVED
    P->>S: SUBMIT|PhongA
    S-->>P: SCORE|1|2
    O->>S: RESULTS|PhongA
    S-->>O: RESULTS + các dòng R sau khi FINISHED
~~~

Nếu người thi không SUBMIT trước hạn, server tự chấm và phát EVENT|AUTO_SCORE khi kết nối còn mở. Nếu WAIT đủ khoảng 30 giây mà phòng vẫn WAITING, server trả WAITING và client gửi WAIT lại. Khi người thi kết nối lại, EXAM cung cấp lại đề và lựa chọn đã lưu; STATUS trả điểm nếu đã chấm.

### 7.4. Trình tự luyện tập

~~~text
PRACTICE -> PQUESTION -> PANSWER (có thể lặp/sửa) -> PSUBMIT -> PSCORE
                                              -> hết giờ -> điểm được lưu -> PSTATUS -> PSCORE
~~~

Luyện tập bắt đầu tính giờ ngay khi PRACTICE thành công; không cần phòng hoặc START.

## 8. Mã lỗi

Mọi lỗi ứng dụng có dạng **ERR|CODE**. Không phải lệnh nào cũng có thể trả tất cả các mã dưới đây.

| Mã | Ý nghĩa |
|---|---|
| BAD_REQUEST | Sai số trường, giá trị/định dạng không hợp lệ hoặc cố REGISTER/LOGIN trên kết nối đã xác thực. |
| TOO_MANY_FIELDS | Thông điệp có hơn 16 trường sau khi tách. |
| UNKNOWN_COMMAND | Lệnh không hỗ trợ sau khi đã đăng nhập. |
| LOGIN_REQUIRED | Gửi lệnh nghiệp vụ khi chưa đăng nhập. |
| CREDENTIALS | Sai tên tài khoản hoặc mật khẩu. |
| ALREADY_LOGGED_IN | Tài khoản đã đăng nhập trên một kết nối khác. |
| ADMIN_CODE | Mã đăng ký admin sai. |
| USER_EXISTS | Username đã tồn tại. |
| OLD_PASSWORD | Mật khẩu cũ sai hoặc không đổi được mật khẩu. |
| PASSWORD_HASH | Không tạo được giá trị băm mật khẩu. |
| FORBIDDEN | Không có quyền thực hiện thao tác. |
| NOT_ENOUGH_QUESTIONS | Ngân hàng không đủ câu cho phòng/bài luyện tập. |
| ROOM_EXISTS | Tên phòng đã tồn tại. |
| ROOM_NOT_FOUND | Không tìm thấy phòng. |
| ROOM_NOT_WAITING | Phòng không còn ở trạng thái WAITING. |
| ROOM_FULL | Phòng đã đủ 32 người. |
| NO_PARTICIPANTS | Chủ phòng START khi chưa có người JOIN. |
| NOT_PARTICIPANT | Người dùng không thuộc phòng. |
| NOT_RUNNING | Không lấy được đề vì phòng không ở trạng thái RUNNING. |
| EXAM_CLOSED | Bài thi chưa hoặc không còn mở để trả lời/nộp. |
| RESULT_NOT_READY | Phòng đã kết thúc nhưng người hỏi chưa có điểm. |
| ROOM_NOT_FINISHED | Chưa được xem bảng điểm vì phòng chưa FINISHED. |
| PRACTICE_NOT_FOUND | Không có bài luyện tập thuộc người dùng với mã đã gửi. |
| PRACTICE_CLOSED | Bài luyện tập không còn mở để lấy câu/trả lời/nộp. |
| DB | Lỗi thao tác SQLite. |

Lỗi khung TCP hoặc mất kết nối có thể làm recv_frame thất bại và đóng luồng xử lý mà không gửi ERR.

## 9. Quyết định thiết kế và giới hạn

1. **TCP + length prefix:** TCP truyền có thứ tự và tin cậy nhưng chỉ cung cấp luồng byte; độ dài 4 byte xác định ranh giới từng thông điệp ứng dụng.
2. **Nội dung dạng văn bản:** dễ đọc và demo. Vì dùng dấu | phân trường, các trường không được chứa dấu này hoặc xuống dòng; hiện chưa có escaping.
3. **Request/response + sự kiện:** lệnh thường có một phản hồi; sự kiện hết giờ cần được server gửi chủ động nên có EVENT riêng.
4. **Phiên gắn với socket:** user_id/role được giữ phía server; LOGIN đồng thời cùng tài khoản bị từ chối. Đăng xuất hoặc ngắt kết nối giải phóng phiên; dữ liệu bài làm vẫn trong SQLite.
5. **An toàn:** mật khẩu được băm khi lưu nhưng hiện truyền qua TCP chưa mã hóa TLS. Chỉ nên demo trên mạng tin cậy.
6. **Khả năng mở rộng:** chưa có trường phiên bản giao thức, request ID, escaping, TLS, nén hoặc khung lớn hơn MAX_FRAME. Tối đa 50 câu/phòng, 32 người/phòng; thời lượng 10–86.400 giây.

Những giới hạn này mô tả code đang chạy, không phải các tính năng đã triển khai.

## 10. Vị trí triển khai, kiểm thử và liên hệ bài giảng

| Thành phần | Vị trí |
|---|---|
| Khung TCP, byte order, gửi/nhận đủ byte | net.h:22–28; net.c:32–63 |
| Tách trường và kiểm tra dữ liệu | server.c:94–110; net.c:64–70 |
| Phiên đăng nhập, chặn trùng tài khoản | server.c:24–41, 280–304 |
| Lệnh, quyền, phòng thi, luyện tập | server.c:260–561 |
| Chấm bài, hết giờ, sự kiện tự động | server.c:153–263, 564–572 |
| Vòng đời socket server và xử lý từng client | server.c:575–658 |
| Giao diện thi và bốn lựa chọn | client.c:173–262 |
| Kiểm thử tích hợp | tests/test_integration.py |

Sau khi biên dịch, chạy **python tests/test_integration.py**. Bộ kiểm thử dùng server/client thật để kiểm tra chia/gộp khung, đăng nhập và chặn trùng tài khoản, quyền, các lựa chọn A–D trong đề, hiển thị client, phòng thi, luyện tập, nộp sớm, tự nộp, EVENT và dữ liệu sau restart.

**Liên hệ bài giảng:** lec02 slide 17 nêu loại, cú pháp, ý nghĩa và quy tắc trao đổi thông điệp; lec04 slide 48 nêu length prefixing để xử lý luồng byte TCP; lec04 slide 53–64 trình bày định dạng, trình tự, cách xử lý và máy trạng thái giao thức. Tài liệu này cụ thể hóa những nội dung đó cho ứng dụng thi trắc nghiệm.
