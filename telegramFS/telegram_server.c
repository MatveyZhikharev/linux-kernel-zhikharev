#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define NUM_CHATS 3
#define MAX_HISTORY 10

/* Структура должна точно совпадать с той, что в модуле */
struct tg_msg {
    int id;       
    int chat_id;  
    int type;     // 0 = READ, 1 = WRITE
    int len;
    char buf[512];
};

/* Хранилище сообщений чата */
struct chat_db {
    char msgs[MAX_HISTORY][256];
    int msg_count;
} db[NUM_CHATS];

int main() {
    int fd = open("/proc/telegram/ctrl", O_RDWR);
    if (fd < 0) {
        perror("Не удалось открыть /proc/telegram/ctrl ! Модуль загружен?");
        return 1;
    }
    
    printf("Telegram_Server запущен. Обработка чатов 1..%d\n", NUM_CHATS);

    while (1) {
        struct tg_msg msg;
        
        // Блокирующее чтение, пока модуль не пришлет запрос
        ssize_t ret = read(fd, &msg, sizeof(msg));
        if (ret != sizeof(msg)) {
            continue;
        }

        if (msg.chat_id < 1 || msg.chat_id > NUM_CHATS) continue;
        int idx = msg.chat_id - 1;

        if (msg.type == 0) { 
            // КЛИЕНТ ЧИТАЕТ ИСТОРИЮ (cat)
            msg.len = 0;
            if (db[idx].msg_count == 0) {
                msg.len = snprintf(msg.buf, sizeof(msg.buf), "В чате пока нет сообщений.\n");
            } else {
                for (int i = 0; i < db[idx].msg_count; i++) {
                    int added = snprintf(msg.buf + msg.len, sizeof(msg.buf) - msg.len,
                                         "[User] %s\n", db[idx].msgs[i]);
                    msg.len += added;
                }
            }
            write(fd, &msg, sizeof(msg)); // возвращаем ядру
            
        } else if (msg.type == 1) { 
            // КЛИЕНТ ПИШЕТ СООБЩЕНИЕ (echo)
            if (msg.len > 0 && msg.buf[msg.len-1] == '\n') {
                msg.buf[msg.len-1] = '\0'; // убираем перенос строки от echo
            }
            
            if (db[idx].msg_count < MAX_HISTORY) {
                strncpy(db[idx].msgs[db[idx].msg_count], msg.buf, 255);
                db[idx].msg_count++;
            } else {
                // Сдвигаем историю (fifo)
                for (int i = 1; i < MAX_HISTORY; i++) {
                    strcpy(db[idx].msgs[i-1], db[idx].msgs[i]);
                }
                strncpy(db[idx].msgs[MAX_HISTORY-1], msg.buf, 255);
            }
            write(fd, &msg, sizeof(msg)); // подтверждаем запись ядру
        }
    }
    close(fd);
    return 0;
}
