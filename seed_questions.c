#include "seed_questions.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text, *a, *b, *c, *d, *correct, *topic, *difficulty;
} SeedQuestion;

static const SeedQuestion starter_questions[] = {
    {"2 + 3 bằng bao nhiêu?", "4", "5", "6", "7", "B", "toan_hoc", "easy"},
    {"10 - 4 bằng bao nhiêu?", "4", "5", "6", "7", "C", "toan_hoc", "easy"},
    {"3 nhân 2 bằng bao nhiêu?", "6", "5", "7", "8", "A", "toan_hoc", "easy"},
    {"Hình tam giác có mấy cạnh?", "2", "4", "5", "3", "D", "toan_hoc", "easy"},
    {"Một giờ có bao nhiêu phút?", "30", "45", "60", "90", "C", "toan_hoc", "easy"},
    {"Một tuần có mấy ngày?", "5", "7", "6", "8", "B", "doi_song", "easy"},
    {"Một năm có mấy tháng?", "12", "10", "11", "13", "A", "doi_song", "easy"},
    {"Con vật nào kêu meo meo?", "Chó", "Gà", "Vịt", "Mèo", "D", "con_vat", "easy"},
    {"Con vật nào thường cho sữa?", "Gà", "Vịt", "Bò", "Cá", "C", "con_vat", "easy"},
    {"Con vật nào sống dưới nước?", "Cá", "Chó", "Mèo", "Gà", "A", "con_vat", "easy"},
    {"Con vật nào có vòi dài?", "Hổ", "Voi", "Thỏ", "Mèo", "B", "con_vat", "easy"},
    {"Bộ phận nào của cây hút nước?", "Lá", "Hoa", "Quả", "Rễ", "D", "cay_coi", "easy"},
    {"Cây nào cho hạt gạo?", "Cây lúa", "Cây cam", "Cây xoài", "Cây dừa", "A", "cay_coi", "easy"},
    {"Quả nào thường vàng khi chín?", "Dưa hấu", "Chuối", "Cà tím", "Thanh long", "B", "cay_coi", "easy"},
    {"Cây xanh cần gì để quang hợp?", "Muối", "Dầu ăn", "Ánh sáng", "Nhựa", "C", "cay_coi", "easy"},
    {"Đèn giao thông màu đỏ yêu cầu gì?", "Dừng lại", "Đi tiếp", "Tăng tốc", "Vượt xe", "A", "doi_song", "easy"},
    {"Khi sang đường, nên đi ở đâu?", "Giữa đường", "Chỗ khuất", "Lòng đường", "Vạch qua đường", "D", "doi_song", "easy"},
    {"Trước khi ăn, ta nên làm gì?", "Đi ngủ", "Rửa tay", "Chơi game", "Chạy bộ", "B", "doi_song", "easy"},
    {"Dùng vật nào để xem giờ?", "Cái bát", "Cái thìa", "Đồng hồ", "Quyển vở", "C", "doi_song", "easy"},
    {"Sữa chua thường được làm từ gì?", "Gạo", "Ngô", "Khoai", "Sữa", "D", "doi_song", "easy"}
};

int seed_questions(sqlite3 *db) {
    const char *skip = getenv("EXAM_SKIP_SEED");
    if (skip && strcmp(skip, "1") == 0) return 1;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM questions", -1, &stmt, NULL) != SQLITE_OK) return 0;
    int result = sqlite3_step(stmt);
    int count = result == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    if (count < 0) return 0;
    if (count > 0) return 1;

    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) return 0;
    const char *sql = "INSERT INTO questions(text,a,b,c,d,correct,topic,difficulty) VALUES(?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return 0;
    }
    int ok = 1;
    for (size_t i = 0; i < sizeof(starter_questions) / sizeof(starter_questions[0]); ++i) {
        const SeedQuestion *q = &starter_questions[i];
        const char *values[] = {q->text, q->a, q->b, q->c, q->d, q->correct, q->topic, q->difficulty};
        for (int j = 0; j < 8; ++j)
            if (sqlite3_bind_text(stmt, j + 1, values[j], -1, SQLITE_STATIC) != SQLITE_OK) ok = 0;
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) { ok = 0; break; }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);
    if (ok && sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK) return 1;
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    return 0;
}
