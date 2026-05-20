#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define NUM_CHATS 3

/* Протокол обмена между ядром и пользовательской программой */
struct tg_msg {
    int id;       // не используется (зарезервировано)
    int chat_id;  // 1, 2, 3
    int type;     // 0 = READ, 1 = WRITE
    int len;
    char buf[512];
};

struct chat_state {
    struct mutex lock;
    struct tg_msg msg;
    int req_ready;
    int req_done;
    wait_queue_head_t wq;
};

static struct chat_state chats[NUM_CHATS];
static DECLARE_WAIT_QUEUE_HEAD(ctrl_wq);
static DEFINE_MUTEX(global_lock);

static struct proc_dir_entry *tg_dir;
static struct proc_dir_entry *ctrl_node;
static struct proc_dir_entry *chat_nodes[NUM_CHATS];

static int has_pending_request(void)
{
    int i;
    for (i = 0; i < NUM_CHATS; i++)
        if (chats[i].req_ready) return 1;
    return 0;
}

// ---------------------------------------------------------
// Операции контрольного файла (для сервера user-space)
// ---------------------------------------------------------

static ssize_t ctrl_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    int i;
    struct tg_msg msg;

    if (count != sizeof(struct tg_msg))
        return -EINVAL;

    // Сервер засыпает, пока ни в одном чате не появится запрос
    if (wait_event_interruptible(ctrl_wq, has_pending_request()))
        return -ERESTARTSYS;

    mutex_lock(&global_lock);
    for (i = 0; i < NUM_CHATS; i++) {
        if (chats[i].req_ready) {
            msg = chats[i].msg;
            chats[i].req_ready = 0; // запрос забран сервером
            break;
        }
    }
    mutex_unlock(&global_lock);

    if (copy_to_user(buf, &msg, sizeof(msg)))
        return -EFAULT;

    return sizeof(msg);
}

static ssize_t ctrl_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct tg_msg msg;

    if (count != sizeof(struct tg_msg))
        return -EINVAL;

    if (copy_from_user(&msg, buf, sizeof(msg)))
        return -EFAULT;

    // Сервер прислал ответ - распаковываем и будим спящий cat/echo
    if (msg.chat_id >= 1 && msg.chat_id <= NUM_CHATS) {
        int idx = msg.chat_id - 1;
        chats[idx].msg = msg;
        chats[idx].req_done = 1;
        wake_up_interruptible(&chats[idx].wq);
    }
    
    return count;
}

static const struct proc_ops ctrl_fops = {
    .proc_read = ctrl_read,
    .proc_write = ctrl_write,
};

// ---------------------------------------------------------
// Операции файлов чатов (для команд cat и echo)
// ---------------------------------------------------------

static ssize_t chat_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    int idx = (long)pde_data(file_inode(file)) - 1;
    struct chat_state *c = &chats[idx];
    ssize_t ret;

    // `cat` делает read пока не вернется 0
    // Поэтому если ppos > 0, мы всё отдали.
    if (*ppos > 0) return 0; 

    mutex_lock(&c->lock);
    
    c->msg.chat_id = idx + 1;
    c->msg.type = 0; // READ REQ
    c->msg.len = 0;
    c->req_done = 0;
    c->req_ready = 1;

    wake_up_interruptible(&ctrl_wq); // будим сервер

    // Ждем ответ от сервера (историю сообщений)
    if (wait_event_interruptible(c->wq, c->req_done)) {
        c->req_ready = 0;
        mutex_unlock(&c->lock);
        return -ERESTARTSYS;
    }

    ret = simple_read_from_buffer(buf, count, ppos, c->msg.buf, c->msg.len);
    mutex_unlock(&c->lock);
    
    return ret;
}

static ssize_t chat_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int idx = (long)pde_data(file_inode(file)) - 1;
    struct chat_state *c = &chats[idx];
    ssize_t ret;

    mutex_lock(&c->lock);
    
    c->msg.chat_id = idx + 1;
    c->msg.type = 1; // WRITE REQ
    
    c->msg.len = count < sizeof(c->msg.buf) - 1 ? count : sizeof(c->msg.buf) - 1;
    if (copy_from_user(c->msg.buf, buf, c->msg.len)) {
        mutex_unlock(&c->lock);
        return -EFAULT;
    }
    c->msg.buf[c->msg.len] = '\0';
    
    c->req_done = 0;
    c->req_ready = 1;

    wake_up_interruptible(&ctrl_wq); // будим сервер

    // Ждем подтверждения сервера, что сообщение принято
    if (wait_event_interruptible(c->wq, c->req_done)) {
        c->req_ready = 0;
        mutex_unlock(&c->lock);
        return -ERESTARTSYS;
    }

    ret = count; // сообщаем, что все байты "записаны"
    *ppos += ret;
    mutex_unlock(&c->lock);
    
    return ret;
}

static const struct proc_ops chat_fops = {
    .proc_read = chat_read,
    .proc_write = chat_write,
};

// ---------------------------------------------------------
// Загрузка и выгрузка модуля
// ---------------------------------------------------------

static int __init tg_init(void)
{
    int i;
    char name[32];

    for (i = 0; i < NUM_CHATS; i++) {
        mutex_init(&chats[i].lock);
        init_waitqueue_head(&chats[i].wq);
    }

    tg_dir = proc_mkdir("telegram", NULL);
    if (!tg_dir) return -ENOMEM;

    ctrl_node = proc_create("ctrl", 0666, tg_dir, &ctrl_fops);
    for (i = 0; i < NUM_CHATS; i++) {
        snprintf(name, sizeof(name), "chat_%d", i + 1);
        chat_nodes[i] = proc_create_data(name, 0666, tg_dir, &chat_fops, (void *)(long)(i + 1));
    }

    printk(KERN_INFO "Telegram FS module loaded\n");
    return 0;
}

static void __exit tg_exit(void)
{
    int i;
    char name[32];

    proc_remove(ctrl_node);
    for (i = 0; i < NUM_CHATS; i++) {
        snprintf(name, sizeof(name), "chat_%d", i + 1);
        remove_proc_entry(name, tg_dir);
    }
    proc_remove(tg_dir);

    printk(KERN_INFO "Telegram FS module unloaded\n");
}

module_init(tg_init);
module_exit(tg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("student");
MODULE_DESCRIPTION("Telegram File Interface module");
