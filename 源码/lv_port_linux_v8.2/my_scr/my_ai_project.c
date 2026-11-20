#include"../my_inc/my_ai_project.h"
#include"lvgl/lvgl.h"
#include"cJSON.h"
#include<curl/curl.h>
#include<pthread.h>
#include<time.h>
#include<stdlib.h>
#include<string.h>   /* strdup/strlen/strcmp/memcpy：缺了会被隐式声明成返回 int 的函数 */
#include <stdbool.h>
#include "pinyin_data.h"   /* 拼音->汉字候选表（my_data/pinyin_data.h，生成器生成） */

// 区域	         控件类型	                          文字
// 顶部标题	  标签 (Label)	        "用户登录 / User login"
// 用户名行	标签 + 文本框	       "user name" + 输入框
// 密码行	 标签 + 文本框（密码模式）	"password" + 输入框
// 底部行	  复选框 + 复选框	             "同意协议" + "记住密码"
// 底部中间	    按钮	              "登录"



/*
 * 全局对象句柄
 * scr1：页面1(登录页)的容器对象句柄
 * scr2：页面2(二级页面)的容器对象句柄
 * lv_obj_t 是LVGL中所有界面控件的统一对象类型，保存控件的地址信息
 */
lv_obj_t *scr1=NULL;
lv_obj_t *scr2=NULL;

// 前向声明，告诉编译器这个函数存在
static void add_message_bubble(const char *text, bool is_user);
static lv_obj_t *g_user_textarea=NULL;//用户指针
static lv_obj_t *g_pwd_textarea=NULL;//密码指针
static lv_obj_t *g_chat_container = NULL; // 聊天消息容器（用于滚动）
static lv_obj_t *g_chat_input = NULL;     // 聊天输入框
static lv_obj_t *g_login_kb = NULL;       // 登录页键盘（挂在 lv_scr_act() 上，切换页面要手动收起）
static lv_obj_t *g_chat_kb = NULL;        // 聊天页键盘（同上）

// ============ 拼音输入法全局状态 ============
static lv_obj_t *g_py_panel = NULL;       // 拼音键盘面板
static lv_obj_t *g_py_label = NULL;       // 拼音显示标签
static lv_obj_t *g_py_cand[6] = {0};      // 候选字按钮（最多 6 个）
static char g_py_buf[16] = {0};           // 当前拼音串
static int  g_py_n = 0;                   // 拼音长度
static int  g_py_cand_n = 0;              // 候选个数
static char g_py_cand_str[6][8] = {{0}};  // 候选内容（UTF-8，最多 2 个汉字 + '\0'）
static bool g_py_open = false;            // 拼音面板是否打开
static int  g_py_page = 0;                // 0=字母页 1=符号页
static lv_obj_t *g_py_page_letters = NULL;// 字母页容器
static lv_obj_t *g_py_page_syms = NULL;   // 符号页容器

// 常用词（拼音 -> 词组），让输入更像正常输入法：打 nihao 直接出"你好"
typedef struct { const char *py; const char *hz; } py_phrase_t;
static const py_phrase_t py_phrases[] = {
    {"nihao",   "你好"},   {"ninhao",  "您好"},   {"zaijian", "再见"},
    {"xiexie",  "谢谢"},   {"bangzhu", "帮助"},   {"duibuqi", "对不起"},
    {"meiguanxi", "没关系"}, {"keyi",  "可以"},    {"buxing",  "不行"},
    {"meiyou",  "没有"},   {"xihuan",  "喜欢"},   {"zhongwen","中文"},
    {"yingwen", "英文"},   {"ceshi",   "测试"},   {"jintian", "今天"},
    {"mingtian","明天"},   {"zuotian", "昨天"},   {"tianqi",  "天气"},
    {"pengyou", "朋友"},   {"tongxue", "同学"},   {"laoshi",  "老师"},
    {"woaini",  "我爱你"}, {"buzhidao","不知道"}, {"zhidao",  "知道"},
    {"weishenme","为什么"}, {"zenme",  "怎么"},   {"shenme",  "什么"},
    {"zhege",   "这个"},   {"nage",    "那个"},   {"xianzai", "现在"},
    {"shijian", "时间"},   {"shouji",  "手机"},   {"diannao", "电脑"},
    {"wangluo", "网络"},   {"youxi",   "游戏"},   {"yinyue",  "音乐"},
    {"dongxi",  "东西"},   {"haode",   "好的"},   {"meishi",  "没事"},
    {"xiexieni","谢谢你"}, {"bangbangwo","帮帮我"},
};
#define PY_PHRASE_LEN (sizeof(py_phrases) / sizeof(py_phrases[0]))

// 单字母快速候选（最常用字优先）：打 n 出"你"、打 h 出"好"，这才是正常输入法的体验。
// 26 个字母各 6 个最常用字，全部在 hei 字体覆盖范围内。
static const char *py_hot[26] = {
    /* a */ "啊爱安按阿哎",
    /* b */ "不把被本比别",
    /* c */ "从出成吃常次",
    /* d */ "的得到都大对",
    /* e */ "而二儿额恩饿",
    /* f */ "发分方法反放",
    /* g */ "个过国高跟更",
    /* h */ "好和还会很后",
    /* i */ "爱挨矮唉哀碍",
    /* j */ "就家见叫进觉",
    /* k */ "看可开快空考",
    /* l */ "了来里两老路",
    /* m */ "吗没们买明每",
    /* n */ "你那能呢哪拿",
    /* o */ "哦噢偶欧呕鸥",
    /* p */ "朋平怕跑片皮",
    /* q */ "去起前请气全",
    /* r */ "人日让如然认",
    /* s */ "是说上什时事",
    /* t */ "他天同听她太",
    /* u */ "五物无务屋乌",
    /* v */ "五物无务屋乌",
    /* w */ "我为无万王问",
    /* x */ "想下现小学习",
    /* y */ "一有要以也用",
    /* z */ "在这中知走做",
};

// 聊天界面状态
static lv_obj_t *g_thinking = NULL;       // "AI 正在输入..." 提示标签
static bool g_ai_busy = false;            // AI 是否正在回复（防重复发送）
static lv_obj_t *g_send_btn = NULL;       // 聊天发送按钮（键盘弹出时要一起抬升）
static lv_obj_t *g_cand_panel = NULL;     // 已弃用（v8.3 官方 IME 候选栏），恒为 NULL，仅保留防御性检查

// 用于线程通信
static char *pending_reply = NULL;//定义一个相当于子线程通过ai答复存放的柜子
static pthread_mutex_t reply_mutex = PTHREAD_MUTEX_INITIALIZER;//定义一把互斥锁----防止柜子里面数据拿放错误
static lv_timer_t *check_timer = NULL;//定义一个定时器用于定时监测--比如100ms去取出存在柜子里面的消息


static lv_coord_t chat_y = 0; // 当前消息区域的Y坐标（累加）

// 声明外部图片变量（由 my_data/login_bg.c 提供）
LV_IMG_DECLARE(login_bg);
// 如有第二张图可添加：LV_IMG_DECLARE(chat_bg);
LV_IMG_DECLARE(chat_bg);

// 中文字体：自定义全汉字字体（my_data/lv_font_hei16_cjk.c，黑体16px，覆盖全部常用汉字+标点）。
// 之前用 LVGL 内置 simsun_16_cjk（仅 1000 字，缺"帮/吗/么/们"和。？！等标点会掉字），已弃用。
LV_FONT_DECLARE(lv_font_hei16_cjk);


/* 前向声明回调函数（解决编译顺序问题） */
static void btn_to_screen2_cb(lv_event_t *e);//按钮1,跳转到聊天界面
static void btn_to_screen1_cb(lv_event_t *e);//按钮2,跳转到界面登陆界面
static void send_btn_cb(lv_event_t *e);//发送消息按钮的回调函数,方便ai回复响应
static void ta_kb_event_cb(lv_event_t *e);//输入框聚焦/失焦时自动弹出或收起键盘
static void bind_keyboard(lv_obj_t *ta, lv_obj_t *kb);//把一个输入框和键盘关联起来
static void chat_kb_ready_cb(lv_event_t *e);//键盘"OK"键：收起键盘并发送消息
static void chat_kb_cancel_cb(lv_event_t *e);//键盘"x"键：只收起键盘
static void py_toggle_cb(lv_event_t *e);     //"中/EN"按钮：开关拼音面板
static void py_toggle(bool show);            //开关拼音面板
static void py_key_cb(lv_event_t *e);        //拼音键盘按键
static void py_sym_cb(lv_event_t *e);        //符号/数字键：直接插入输入框
static void py_page_cb(lv_event_t *e);       //"符/abc"：字母页<->符号页切换
static void py_cand_cb(lv_event_t *e);       //拼音候选字按钮
static void py_refresh(void);                //根据当前拼音刷新候选字
static void create_pinyin_panel(void);       //创建拼音键盘面板
static void clear_chat_cb(lv_event_t *e);    //"清"按钮：清空对话与 AI 记忆
static void chat_kb_layout(bool up);         //键盘弹出时把输入框抬到键盘上方

/* 内部函数声明为 static（只在本文件使用） */
static void create_screen1(void);
static void create_screen2(void);

// ==================== AI 核心功能（历史管理、网络请求、JSON 解析） ====================

// 定义链表结构-----对话消息节点
typedef struct chat_node
{
    char *role;  //"user"或"assistant",存放用户名还是ai名
    char *content; //消息内容
    struct chat_node *next;//定义结构体类型指针指向下一位
} chat_node_t;

//链表头指针(全局)
static chat_node_t *history_head = NULL;

//限制链表长度不超过max_len,删除最旧的节点------对于超过max_len长度的对话会释放掉
//参数:max_len  允许的最大链表长度
//功能:如果链表长度超过max_len,则从头部依次删除节点直到长度符合
static void trim_history(int max_len){
    int len=0;
    chat_node_t *cur=history_head;
    while(cur!=NULL){
        len++;
        cur=cur->next;

    }//计算当前长度
    while(len>max_len){
        chat_node_t*tmp=history_head;
        history_head=history_head->next;//头指针向后移动
        //释放掉里超过max_len的节点
        free(tmp->role);
        free(tmp->content);
        free(tmp);
        //更新长度
        len--;

    }
}

//添加结点--尾插法
//参数:role -角色(如""user"或"")
//功能:创建新节点,追加到链表末尾------------用来存储与ai历史对话
static void add_history(const char *role,const char *content){

    chat_node_t *new_node = malloc(sizeof(chat_node_t));
    if (!new_node) return;//分配失败则直接返回
    new_node->role = strdup(role);
    new_node->content = strdup(content);
    new_node->next = NULL;

    if (history_head == NULL) {
        history_head = new_node;
    } else {
        chat_node_t *cur = history_head;
        while (cur->next != NULL) cur = cur->next;
        cur->next = new_node;
    }
    // 限制链表长度不超过 20
    trim_history(20);
}

//释放整个链表
//功能:遍历链表,释放每个role,content以及节点本身,最后将头指针置空
static void free_history(void){
    chat_node_t *cur = history_head;
    while (cur != NULL) {
        chat_node_t *tmp = cur;
        cur = cur->next;//先保存下一个节点
        free(tmp->role);//释放角色字符串
        free(tmp->content);//释放内容字符串
        free(tmp);//释放节点
    }
    history_head = NULL;//避免野指针
}

//---------------------------------------------------------------
//专门存放从deepseek api服务器收到的http响应
typedef struct 
{
    char *data;//存放响应文本
    size_t size;//记录存放的字节数
} ResponseData;

//构造回调函数----将每次接受到的数据都追加到responsdata中
// 参数说明：
// ptr      ：本次收到的原始数据块
// size, nmemb ：本次数据的字节数 = size * nmemb
// userdata ：你传进去的 ResponseData 结构体指针
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    // 1. 把 userdata 转换回我们自己的结构体指针
    ResponseData *resp = (ResponseData *)userdata;
    
    // 2. 计算本次收到的字节数
    size_t total_size = size * nmemb;
    
    // 3. 尝试扩大缓冲区（原有数据 + 新数据 + 1字节留给 '\0'）
    char *new_data = realloc(resp->data, resp->size + total_size + 1);
    
    // 4. 如果内存不足，返回 0 告诉 libcurl 出错
    if (!new_data) return 0;
    
    // 5. 更新结构体中的指针，指向新分配的内存
    resp->data = new_data;
    
    // 6. 把新数据复制到已有数据的末尾
    memcpy(resp->data + resp->size, ptr, total_size);
    
    // 7. 更新已存储的总字节数
    resp->size += total_size;
    
    // 8. 在末尾加上 '\0'，使数据成为合法的 C 字符串
    resp->data[resp->size] = '\0';
    
    // 9. 返回实际处理了的字节数，必须等于 total_size 才能告诉 libcurl“我处理好了”
    return total_size;
}

//发送HTTP POST 请求,返回服务器原始响应
static char *send_request(const char* json_payload){
    CURL *curl;
    CURLcode res;

    //初始化响应存储结构
    ResponseData response = { .data = malloc(1), .size = 0 };
    if (!response.data) return NULL;
    response.data[0] = '\0';

    //全局初始化licurl库
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    if (!curl) {
        free(response.data);
        return NULL;
    }

    // 设置请求头
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // 从环境变量 DEEPSEEK_API_KEY 或下方占位符填入自己的 Key（切勿提交真实 Key）
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization:Bearer YOUR_DEEPSEEK_API_KEY");// ⚠️ 重要：这里需要你的 API Key，暂时先放个占位符
    headers = curl_slist_append(headers, auth_header);

    // 配置 curl 选项
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    // 测试阶段忽略 SSL 证书验证
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // 执行请求
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_global_cleanup();

    if (res != CURLE_OK) {
        fprintf(stderr, "curl 请求失败: %s\n", curl_easy_strerror(res));
        free(response.data);
        return NULL;
    }

    return response.data;  // 调用者负责 free
}

// 构建 DeepSeek API 请求体,将要发的的消息装到json中
static char* build_json_payload(const char* user_msg) {
    // 1. 创建根对象
    cJSON *root = cJSON_CreateObject();

    // 2. 添加 model 字段
    cJSON_AddStringToObject(root, "model", "deepseek-chat");

    // 3. 创建 messages 数组
    cJSON *messages = cJSON_CreateArray();

    // 3.5 系统提示词：强制 AI 用中文回答。
    //     屏幕键盘只能输入英文，若不加这条，发英文 AI 会回英文。
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", "你是一个智能对话助手，无论用户用什么语言提问，请始终用中文回答。");
    cJSON_AddItemToArray(messages, sys_msg);

    //4.遍历链表,加入所有历史消息
    chat_node_t *cur=history_head;

    while(cur!=NULL){
        //创建cjson对象,用于存放单条消息
        cJSON*msg_obj=cJSON_CreateObject();
        //向创建的对象添加键值对
        cJSON_AddStringToObject(msg_obj,"role",cur->role);
        //向该对象添加键值对:键:content,值为具体的聊天内容
        cJSON_AddStringToObject(msg_obj,"content",cur->content);
        //将组装好的消息对象（msg_obj）添加到父级数组（messages）中，作为数组的一个元素
        cJSON_AddItemToArray(messages,msg_obj);
        //指针后移,指向链表的下一个节点.
        cur=cur->next;
    }

    // 5. 创建当前用户消息对象
    cJSON *current_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(current_msg, "role", "user");
    cJSON_AddStringToObject(current_msg, "content", user_msg);

    // 6. 把消息加入json数组中
    cJSON_AddItemToArray(messages, current_msg);

    // 7. 把数组加入根对象里
    cJSON_AddItemToObject(root, "messages", messages);

    // 8. 添加可选参数
    cJSON_AddNumberToObject(root, "max_tokens", 1024);
    cJSON_AddNumberToObject(root, "temperature", 0.7);
    cJSON_AddBoolToObject(root, "enable_search", 1);  // 开启联网搜索
    // 9. 转为字符串（不在这里打印，也不释放）
    char *json_str = cJSON_Print(root);
    
    // 10. 释放 cJSON 对象
    cJSON_Delete(root);
    
    // 11. 返回 JSON 字符串（调用者负责 free）
    return json_str;
}

//解析ai发来的答复json文件进行解析,提取ai回复的问题内容
static char* parse_reply(const char* json_response) {
    cJSON *root = cJSON_Parse(json_response);
    if (!root) {
        return strdup("JSON解析失败");
    }

    // 检查是否有错误字段
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        char *err_str = msg ? strdup(msg->valuestring) : strdup("未知API错误");
        cJSON_Delete(root);
        return err_str;
    }

    // 提取 choices[0].message.content
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(root);
        return strdup("无效响应结构");
    }
    cJSON *first = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(first, "message");
    if (!message) {
        cJSON_Delete(root);
        return strdup("缺少message字段");
    }
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!content || !cJSON_IsString(content)) {
        cJSON_Delete(root);
        return strdup("缺少content字段");
    }

    char *result = strdup(content->valuestring);
    cJSON_Delete(root);
    return result;
}

// 这个函数是 ai 回复（同步调用，用于线程内部）
static char* get_ai_reply(const char* user_msg) {
    // 1. 构造 JSON 字符串
    char *json_payload = build_json_payload(user_msg);
    if (!json_payload) {
        return strdup("构造请求失败");
    }

    // 2. 打印 JSON（方便你调试）
    printf("构造的 JSON:\n%s\n", json_payload);

    // 3. 发送请求，获取原始响应
    char *raw_response = send_request(json_payload);
    free(json_payload);

    if (!raw_response) {
        return strdup("网络请求失败，请检查网络或稍后重试");
    }

    // 4. 打印原始响应（先看看服务器返回了什么）
    printf("服务器原始响应:\n%s\n", raw_response);

    // 5. 解析响应，提取 AI 回复
    char *reply = parse_reply(raw_response);
    free(raw_response);

    if (!reply) {
        return strdup("解析回复失败");
    }
    return reply;
}

// ==================== 线程相关函数 ====================

// 线程执行函数：获取 AI 回复，存入 pending_reply
static void *ai_thread_func(void *arg) {
    const char *user_msg = (const char *)arg;
    char *reply = get_ai_reply(user_msg);
    // 释放用户消息拷贝（在 send_btn_cb 中 strdup 的）
    free((void *)user_msg);

    // 加锁，存入 pending_reply
    pthread_mutex_lock(&reply_mutex);
    if (pending_reply) {
        free(pending_reply);
        pending_reply = NULL;
    }
    pending_reply = reply;  // 可能为 NULL
    pthread_mutex_unlock(&reply_mutex);

    return NULL;
}

// 定时器回调：检查是否有 AI 回复，若有则显示在界面并加入历史
static void check_reply_timer(lv_timer_t *timer) {
    pthread_mutex_lock(&reply_mutex);
    if (pending_reply) {
        // 显示 AI 回复
        add_message_bubble(pending_reply, false);
        // 加入历史
        add_history("assistant", pending_reply);
        free(pending_reply);
        pending_reply = NULL;
        // 恢复可发送状态，隐藏"正在输入"
        g_ai_busy = false;
        if (g_thinking) lv_obj_add_flag(g_thinking, LV_OBJ_FLAG_HIDDEN);
    }
    pthread_mutex_unlock(&reply_mutex);
}

// ==================== 键盘关联（LVGL v8.2 的正确写法） ====================

/*
 * 键盘/拼音面板弹出时，输入框会被盖住看不到字。
 * 这里把输入框和发送按钮整体抬到键盘上方（聊天区同步压缩），收起时恢复。
 */
static void chat_kb_layout(bool up) {
    if (!g_chat_input) return;
    if (up) {
        if (g_chat_container) lv_obj_set_height(g_chat_container, 150);  // 65..215
        lv_obj_align(g_chat_input, LV_ALIGN_BOTTOM_LEFT, 160, -210);     // y=225..270，在键盘(280)上方
        if (g_send_btn) lv_obj_align(g_send_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -210);
    } else {
        if (g_chat_container) lv_obj_set_height(g_chat_container, 300);
        lv_obj_align(g_chat_input, LV_ALIGN_BOTTOM_LEFT, 160, -10);
        if (g_send_btn) lv_obj_align(g_send_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    }
}

/*
 * v8.2 里没有 lv_textarea_set_keyboard()（那是 v8.3 才加的，而且方向也相反）。
 * 正确做法：
 *   1) 用 lv_keyboard_set_textarea(kb, ta) 告诉键盘"你当前服务哪个输入框"；
 *   2) 键盘不会自动弹出，必须自己在输入框的 FOCUSED / DEFOCUSED 事件里显隐它。
 */
static void ta_kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        if (g_py_panel) lv_obj_add_flag(g_py_panel, LV_OBJ_FLAG_HIDDEN);  // 拼音面板与普通键盘互斥
        if (ta == g_chat_input) chat_kb_layout(true);                     // 聊天输入框：把输入区抬起来
        /* 登录页键盘保持常驻，绝不因失焦而隐藏（见下方 DEFOCUSED 分支说明） */
    } else if (code == LV_EVENT_DEFOCUSED) {
        /* 仅聊天页在失焦时收起键盘并复位输入区。
         * 登录页不做此处理：点击键盘按键会使输入框失焦，若在此隐藏键盘，
         * 就会出现“打字时键盘消失”的 bug；登录页改用 OK(✓) 键显式收起（login_kb_ready_cb）。 */
        if (ta == g_chat_input) {
            lv_keyboard_set_textarea(kb, NULL);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
            if (g_cand_panel) lv_obj_add_flag(g_cand_panel, LV_OBJ_FLAG_HIDDEN);  // 候选栏一起收起
            chat_kb_layout(false);
        }
    }
}

static void bind_keyboard(lv_obj_t *ta, lv_obj_t *kb) {
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(ta, ta_kb_event_cb, LV_EVENT_FOCUSED, kb);
    lv_obj_add_event_cb(ta, ta_kb_event_cb, LV_EVENT_DEFOCUSED, kb);
}

// 聊天键盘的"OK/完成"键（LV_SYMBOL_OK）：收起键盘 + 把输入框内容发出去
static void chat_kb_ready_cb(lv_event_t *e) {
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if(g_chat_input) lv_obj_clear_state(g_chat_input, LV_STATE_FOCUSED);
    if(g_cand_panel) lv_obj_add_flag(g_cand_panel, LV_OBJ_FLAG_HIDDEN);
    chat_kb_layout(false);
    send_btn_cb(NULL);   // 复用发送逻辑（回调函数不读 e）
}

// 聊天键盘的"x"关闭键（LV_SYMBOL_CLOSE）：只收起键盘
static void chat_kb_cancel_cb(lv_event_t *e) {
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if(g_chat_input) lv_obj_clear_state(g_chat_input, LV_STATE_FOCUSED);
    if(g_cand_panel) lv_obj_add_flag(g_cand_panel, LV_OBJ_FLAG_HIDDEN);
    chat_kb_layout(false);
}

// 登录键盘的"OK/✓"键（LV_SYMBOL_OK）：仅收起键盘，让被遮挡的登录按钮重新可点；
// 不发送任何内容（登录逻辑由登录按钮触发）。
static void login_kb_ready_cb(lv_event_t *e) {
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if(g_user_textarea) lv_obj_clear_state(g_user_textarea, LV_STATE_FOCUSED);
    if(g_pwd_textarea)  lv_obj_clear_state(g_pwd_textarea,  LV_STATE_FOCUSED);
}

// ==================== 拼音输入法 ====================
// 面板布局（640x200，右下角，避开左侧触摸死区）：
//   y=8:   [拼音显示][候选1][候选2]...[候选6]
//   y=48:  q w e r t y u i o p
//   y=86:  a s d f g h j k l 删
//   y=124: z x c v b n m [空格] [确定]

// 追加一个候选（UTF-8 字符串，最多 7 字节），自动去重
// len 必须显式传入：传 strlen(s) 会把"你那能呢..."整串当候选（候选变成两字+坏字节），
// 单字场景必须传 3。
static void py_add_cand(const char *s, size_t len) {
    if (g_py_cand_n >= 6 || !s) return;
    if (len > 7) len = 7;
    for (int i = 0; i < g_py_cand_n; i++) {   // 去重
        if (strlen(g_py_cand_str[i]) == len && strncmp(g_py_cand_str[i], s, len) == 0) return;
    }
    memcpy(g_py_cand_str[g_py_cand_n], s, len);
    g_py_cand_str[g_py_cand_n][len] = 0;
    g_py_cand_n++;
}

// 从某个音节的汉字串里逐字追加候选（每个字固定 3 字节）
static void py_add_cand_from_hz(const char *hz) {
    while (hz[0] && g_py_cand_n < 6) {
        py_add_cand(hz, 3);
        hz += 3;
    }
}

// 根据 g_py_buf 刷新候选，匹配顺序：
//   1) 常用词精确（nihao -> 你好）
//   2) 单字母：直接用最常用字表（n -> 你那能呢哪拿）
//   3) 音节精确（ni -> 尼你呢泥…）
//   4) 前向前缀（h / ha -> 从最近的 ha 音节取字，保证任何字母组合都有候选）
//   5) 反向前缀（输入 nihao 时命中 ni，补充"你"）
static void py_refresh(void) {
    if (g_py_buf[0]) {
        size_t n = strlen(g_py_buf);

        // 1. 常用词精确匹配
        for (size_t i = 0; i < PY_PHRASE_LEN && g_py_cand_n < 6; i++) {
            if (strcmp(py_phrases[i].py, g_py_buf) == 0) {
                py_add_cand(py_phrases[i].hz, strlen(py_phrases[i].hz));
                break;
            }
        }

        // 2. 单字母：最常用字表（打 n 直接给"你"）
        if (g_py_buf[1] == 0) {
            int idx = g_py_buf[0] - 'a';
            if (idx >= 0 && idx < 26) py_add_cand_from_hz(py_hot[idx]);
        }

        // 3. 音节精确匹配
        const py_entry_t *exact = NULL;
        for (size_t i = 0; i < PY_TABLE_LEN; i++) {
            if (strcmp(py_table[i].py, g_py_buf) == 0) { exact = &py_table[i]; break; }
        }
        if (exact) py_add_cand_from_hz(exact->hz);

        // 3. 前向前缀：取"以缓冲区开头"的最短音节（最接近完成），
        //    保证 h / ha 这类半截拼音也有候选
        if (g_py_cand_n < 6) {
            const py_entry_t *fwd = NULL;
            size_t fwd_len = 0;
            for (size_t i = 0; i < PY_TABLE_LEN; i++) {
                size_t l = strlen(py_table[i].py);
                if (l > n && strncmp(py_table[i].py, g_py_buf, n) == 0) {
                    if (!fwd || l < fwd_len) { fwd = &py_table[i]; fwd_len = l; }
                }
            }
            if (fwd && fwd != exact) py_add_cand_from_hz(fwd->hz);
        }

        // 4. 反向前缀：输入串是多音节时（如 nihao），取"是输入串前缀"的最长音节（ni）
        if (g_py_cand_n < 6) {
            const py_entry_t *rev = NULL;
            size_t rev_len = 0;
            for (size_t i = 0; i < PY_TABLE_LEN; i++) {
                size_t l = strlen(py_table[i].py);
                if (l < n && strncmp(g_py_buf, py_table[i].py, l) == 0) {
                    if (l > rev_len) { rev = &py_table[i]; rev_len = l; }
                }
            }
            if (rev && rev != exact) py_add_cand_from_hz(rev->hz);
        }

        // 6. 词组首字母匹配：输入 "nh" 直接出"你好"（nihao 的首字母 n+h），
        //    解决"n 之后按 h 没有反应"（手机输入法的标准能力）
        if (g_py_cand_n < 6 && n >= 2) {
            for (size_t i = 0; i < PY_PHRASE_LEN && g_py_cand_n < 6; i++) {
                const char *py = py_phrases[i].py;
                char inits[8] = {0};
                int k = 0;
                int prev_vowel = 0;
                for (const char *p = py; *p && k < 7; p++) {
                    int is_v = (*p == 'a' || *p == 'e' || *p == 'i' || *p == 'o' || *p == 'u' || *p == 'v');
                    if (k == 0 || (prev_vowel && !is_v)) inits[k++] = *p;  // 音节起点：开头或元音后的辅音
                    prev_vowel = is_v;
                }
                if (k >= 2 && strncmp(inits, g_py_buf, n) == 0) {
                    py_add_cand(py_phrases[i].hz, strlen(py_phrases[i].hz));
                    break;
                }
            }
        }

        // 7. 兜底：任何输入都至少给出第一个字母的常用字（如 "nh" 这种组合也有候选）
        if (g_py_cand_n < 6) {
            int idx = g_py_buf[0] - 'a';
            if (idx >= 0 && idx < 26) py_add_cand_from_hz(py_hot[idx]);
        }
    }
    g_py_cand_n = (g_py_cand_n > 6) ? 6 : g_py_cand_n;

    if (g_py_label) lv_label_set_text(g_py_label, g_py_buf[0] ? g_py_buf : "拼音");
    for (int k = 0; k < 6; k++) {
        if (!g_py_cand[k]) continue;
        if (k < g_py_cand_n) {
            lv_obj_t *lb = lv_obj_get_child(g_py_cand[k], 0);
            if (lb) lv_label_set_text(lb, g_py_cand_str[k]);
            lv_obj_clear_flag(g_py_cand[k], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_py_cand[k], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// 候选字按钮：点一下把字插入聊天输入框
static void py_cand_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_chat_input && idx < g_py_cand_n) {
        lv_textarea_add_text(g_chat_input, g_py_cand_str[idx]);
        g_py_buf[0] = 0; g_py_n = 0;
        py_refresh();
    }
}

// 拼音键盘按键：字母/删/空格/确定
// 注意：user_data 存的是整数键码（字母 97~122，功能键用负值），
// 不能传字符串指针——之前传局部数组指针导致野指针，字母全部失效。
#define PY_KEY_DEL   (-1)   /* 删 */
#define PY_KEY_SPACE (-2)   /* 空格 */
#define PY_KEY_OK    (-3)   /* 确定 */

static void py_key_cb(lv_event_t *e) {
    int key = (int)(intptr_t)lv_event_get_user_data(e);
    if (key == PY_KEY_DEL) {
        if (g_py_n > 0) {
            g_py_buf[--g_py_n] = 0;
        } else if (g_chat_input) {
            lv_textarea_del_char(g_chat_input);
        }
    } else if (key == PY_KEY_SPACE) {
        if (g_py_cand_n > 0) {
            // 空格 = 上屏第一个候选
            if (g_chat_input) lv_textarea_add_text(g_chat_input, g_py_cand_str[0]);
            g_py_buf[0] = 0; g_py_n = 0;
        } else if (g_chat_input) {
            lv_textarea_add_text(g_chat_input, " ");
        }
    } else if (key == PY_KEY_OK) {
        py_toggle(false);
        return;
    } else if (key >= 'a' && key <= 'z') {
        // 字母
        if (g_py_n < 15) {
            g_py_buf[g_py_n++] = (char)key;
            g_py_buf[g_py_n] = 0;
        }
    }
    py_refresh();
}

// 开关拼音面板（与普通键盘互斥）
static void py_toggle(bool show) {
    g_py_open = show;
    if (!g_py_panel) return;
    if (show) {
        // 每次打开都回到字母页
        g_py_page = 0;
        if (g_py_page_letters) lv_obj_clear_flag(g_py_page_letters, LV_OBJ_FLAG_HIDDEN);
        if (g_py_page_syms)    lv_obj_add_flag(g_py_page_syms, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_py_panel, LV_OBJ_FLAG_HIDDEN);
        if (g_chat_kb)   lv_obj_add_flag(g_chat_kb, LV_OBJ_FLAG_HIDDEN);
        if (g_login_kb)  lv_obj_add_flag(g_login_kb, LV_OBJ_FLAG_HIDDEN);
        if (g_chat_input) lv_obj_clear_state(g_chat_input, LV_STATE_FOCUSED);
        chat_kb_layout(true);   // 输入区抬到面板上方，避免挡住输入框
        py_refresh();
    } else {
        lv_obj_add_flag(g_py_panel, LV_OBJ_FLAG_HIDDEN);
        chat_kb_layout(false);
    }
}

// "中/EN"按钮回调
static void py_toggle_cb(lv_event_t *e) {
    (void)e;
    py_toggle(!g_py_open);
}

// 创建一个按键；ch：字母(97~122)或功能键码(PY_KEY_*)；label 会被 LVGL 复制，可用局部变量
// ==================== 拼音键盘（标准 26 键样式） ====================
// 布局（640x200，右下角，避开左侧触摸死区）：
//   候选栏(白底)：[拼音][候选1..6]         y=6
//   字母页：数字行 + 3 行 QWERTY + 空格/符/确定
//   符号页：常用标点 + 数字 + 删/空格/abc/确定

typedef void (*py_key_evt_t)(lv_event_t *);

// 通用建键：x/y 为像素坐标，w 为宽，ch 为事件 user_data（字母码/功能码/或字符串地址）
static void py_add_key_ex(lv_obj_t *parent, int x, int y, int w, int h, int ch,
                          const char *label, bool special, py_key_evt_t cb) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, special ? lv_color_hex(0xC9C9C9) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_t *lb = lv_label_create(b);
    lv_obj_set_style_text_font(lb, &lv_font_hei16_cjk, 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(0x333333), 0);
    lv_label_set_text(lb, label);
    lv_obj_center(lb);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)ch);
}

// 符号/数字键：把字符直接插入聊天输入框（label 是字符串字面量，指针安全）
static void py_sym_cb(lv_event_t *e) {
    const char *s = (const char *)(intptr_t)lv_event_get_user_data(e);
    if (g_chat_input && s) lv_textarea_add_text(g_chat_input, s);
}

// "符/abc"：字母页 <-> 符号页切换
static void py_page_cb(lv_event_t *e) {
    (void)e;
    g_py_page = !g_py_page;
    if (g_py_page_letters) {
        if (g_py_page) lv_obj_add_flag(g_py_page_letters, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_clear_flag(g_py_page_letters, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_py_page_syms) {
        if (g_py_page) lv_obj_clear_flag(g_py_page_syms, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(g_py_page_syms, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_pinyin_panel(void) {
    g_py_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_py_panel, 640, 200);
    lv_obj_align(g_py_panel, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(g_py_panel, lv_color_hex(0xD8D8D8), 0);  // 浅灰面板（标准输入法配色）
    lv_obj_set_style_radius(g_py_panel, 0, 0);
    lv_obj_add_flag(g_py_panel, LV_OBJ_FLAG_HIDDEN);

    // ---------- 顶部候选栏（白底） ----------
    lv_obj_t *bar = lv_obj_create(g_py_panel);
    lv_obj_set_size(bar, 624, 40);
    lv_obj_set_pos(bar, 8, 4);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(bar, 6, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0xBBBBBB), 0);

    // 拼音显示
    g_py_label = lv_label_create(bar);
    lv_obj_set_pos(g_py_label, 8, 8);
    lv_obj_set_size(g_py_label, 96, 24);
    lv_obj_set_style_text_font(g_py_label, &lv_font_hei16_cjk, 0);
    lv_obj_set_style_text_color(g_py_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(g_py_label, "拼音");

    // 候选字按钮（6 个，浅蓝底蓝字）
    for (int i = 0; i < 6; i++) {
        g_py_cand[i] = lv_btn_create(bar);
        lv_obj_set_size(g_py_cand[i], 80, 30);
        lv_obj_set_pos(g_py_cand[i], 108 + i * 85, 4);
        lv_obj_set_style_bg_color(g_py_cand[i], lv_color_hex(0xEAF2FB), 0);
        lv_obj_set_style_radius(g_py_cand[i], 4, 0);
        lv_obj_t *lb = lv_label_create(g_py_cand[i]);
        lv_obj_set_style_text_font(lb, &lv_font_hei16_cjk, 0);
        lv_obj_set_style_text_color(lb, lv_color_hex(0x1565C0), 0);
        lv_label_set_text(lb, "");
        lv_obj_center(lb);
        lv_obj_add_event_cb(g_py_cand[i], py_cand_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_flag(g_py_cand[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ---------- 字母页 ----------
    g_py_page_letters = lv_obj_create(g_py_panel);
    lv_obj_set_size(g_py_page_letters, 640, 152);
    lv_obj_set_pos(g_py_page_letters, 0, 48);
    lv_obj_set_style_bg_color(g_py_page_letters, lv_color_hex(0xD8D8D8), 0);
    lv_obj_set_style_border_width(g_py_page_letters, 0, 0);

    // 数字行（直接上屏）
    const char *nums = "1234567890";
    for (int i = 0; i < 10; i++) {
        char t[2] = { nums[i], 0 };
        py_add_key_ex(g_py_page_letters, i * 64, 0, 62, 30, 0, t, false, py_sym_cb);
    }
    // QWERTY 三行
    const char *row1 = "qwertyuiop";
    const char *row2 = "asdfghjkl";
    const char *row3 = "zxcvbnm";
    for (int i = 0; i < 10; i++) {
        char t[2] = { row1[i], 0 };
        py_add_key_ex(g_py_page_letters, i * 64, 34, 62, 34, row1[i], t, false, py_key_cb);
    }
    for (int i = 0; i < 9; i++) {
        char t[2] = { row2[i], 0 };
        py_add_key_ex(g_py_page_letters, i * 64, 72, 62, 34, row2[i], t, false, py_key_cb);
    }
    py_add_key_ex(g_py_page_letters, 9 * 64, 72, 62, 34, PY_KEY_DEL, "删", true, py_key_cb);
    for (int i = 0; i < 7; i++) {
        char t[2] = { row3[i], 0 };
        py_add_key_ex(g_py_page_letters, i * 64, 110, 62, 34, row3[i], t, false, py_key_cb);
    }
    py_add_key_ex(g_py_page_letters, 7 * 64, 110, 96, 34, PY_KEY_SPACE, "空格", true, py_key_cb);  // 448..544
    py_add_key_ex(g_py_page_letters, 546, 110, 47, 34, 0, "符", true, py_page_cb);                  // 546..593
    py_add_key_ex(g_py_page_letters, 595, 110, 45, 34, PY_KEY_OK, "确定", true, py_key_cb);          // 595..640

    // ---------- 符号页 ----------
    g_py_page_syms = lv_obj_create(g_py_panel);
    lv_obj_set_size(g_py_page_syms, 640, 152);
    lv_obj_set_pos(g_py_page_syms, 0, 48);
    lv_obj_set_style_bg_color(g_py_page_syms, lv_color_hex(0xD8D8D8), 0);
    lv_obj_set_style_border_width(g_py_page_syms, 0, 0);
    lv_obj_add_flag(g_py_page_syms, LV_OBJ_FLAG_HIDDEN);

    const char *sym1 = "，。？！、；：（）";
    for (int i = 0; i < 10; i++) {
        char t[4] = { sym1[i * 3], sym1[i * 3 + 1], sym1[i * 3 + 2], 0 };
        py_add_key_ex(g_py_page_syms, i * 64, 0, 62, 30, 0, t, false, py_sym_cb);
    }
    // 注意：符号串必须全部是 3 字节 UTF-8 字符，循环按 sym[i*3] 取字，混入 ASCII 会取错/越界
    const char *sym2 = "【】《》「」『』￥％";
    for (int i = 0; i < 10; i++) {
        char t[4] = { sym2[i * 3], sym2[i * 3 + 1], sym2[i * 3 + 2], 0 };
        py_add_key_ex(g_py_page_syms, i * 64, 34, 62, 34, 0, t, false, py_sym_cb);
    }
    // 数字行
    for (int i = 0; i < 10; i++) {
        char t[2] = { nums[i], 0 };
        py_add_key_ex(g_py_page_syms, i * 64, 72, 62, 34, 0, t, false, py_sym_cb);
    }
    // 底部功能键（居中）
    py_add_key_ex(g_py_page_syms, 163, 110, 62, 34, PY_KEY_DEL, "删", true, py_key_cb);
    py_add_key_ex(g_py_page_syms, 227, 110, 128, 34, PY_KEY_SPACE, "空格", true, py_key_cb);
    py_add_key_ex(g_py_page_syms, 357, 110, 62, 34, 0, "abc", true, py_page_cb);
    py_add_key_ex(g_py_page_syms, 421, 110, 62, 34, PY_KEY_OK, "确定", true, py_key_cb);
}

// ==================== 原有界面函数（未改动） ====================

 // ==============创建界面1 登录页面==============================
 
static void create_screen1(void){
    // lv_scr_act()：获取当前正在使用的屏幕对象，作为父对象
    // lv_obj_create(父对象)：创建一个基础容器对象，父对象为lv_scr_act()代表挂载到屏幕上
    scr1 = lv_obj_create(lv_scr_act());

    // 设置登陆界面背景图---也就是scr1
    // 使用新版API：lv_obj_set_style_bg_img_src(对象, 图片, 选择器)
    lv_obj_set_style_bg_img_src(scr1, &login_bg, 0);

    // lv_obj_set_size(对象,宽,高)：设置容器的宽高，单位像素，模拟器窗口800*480，铺满整个屏幕
    lv_obj_set_size(scr1,800,480);

    // lv_obj_set_pos(对象,x,y)：设置对象相对于父对象左上角的坐标，0,0代表对齐父对象左上角
    lv_obj_set_pos(scr1,0,0);

    // 函数:lv_obj_set_style_bg_color(对象,颜色,选择器)：设置对象背景颜色
    // 参数1:函数对象
    // 参数2:lv_color_hex(0xFF0000) 将十六进制颜色转为LVGL颜色类型，0xFF0000红色
    // 最后参数0 代表普通状态（非按下、非聚焦状态）
    // lv_obj_set_style_bg_color(scr1,lv_color_hex(0xFFFFFF),0);

    //--------------1.顶部标题标签 (Label)--------------

    lv_obj_t *label =lv_label_create(scr1);//创建

    lv_label_set_text(label,"user login");//设置文字

    lv_obj_set_style_text_color(label,lv_color_hex(0x333333),0);//设置字体颜色

    lv_obj_set_style_text_font(label,NULL,0);//使用默认字体

    lv_obj_align(label,LV_ALIGN_TOP_MID,0,30);//设置位置，居中，向下偏移30


 //--------------2.设置用户输入行（标签+输入框）--------------
    lv_obj_t *user_label=lv_label_create(scr1);
//设置文本
    lv_label_set_text(user_label,"user name");
//设置颜色深度
    lv_obj_set_style_text_color(user_label,lv_color_hex(0x333333),0);
//设置字体大小
     lv_obj_set_style_text_font(user_label,NULL,0);
//设置位置，居中，向下偏移100
     lv_obj_align(user_label,LV_ALIGN_TOP_MID,0,100);


//2.1创建输入框
     g_user_textarea=lv_textarea_create(scr1);
//设置输入框的大小
    lv_obj_set_size(g_user_textarea,250,50);
//设置输入框的位置
    lv_obj_align(g_user_textarea,LV_ALIGN_TOP_MID,0,140);
//设置输入框的边框大小
    lv_obj_set_style_border_width(g_user_textarea,1,0);
//设置 圆角半径 5px
lv_obj_set_style_radius(g_user_textarea, 5, 0);   
 // 占位文字（灰色提示）     
lv_textarea_set_placeholder_text(g_user_textarea, "input name"); 
// 白色背景
lv_obj_set_style_bg_color(g_user_textarea, lv_color_hex(0xFFFFFF), 0);   


//--------------、3.设置密码输入行（标签+输入框）--------------

 lv_obj_t *pwd_label=lv_label_create(scr1);
//设置文本
    lv_label_set_text(pwd_label,"user passworld");
//设置颜色深度
    lv_obj_set_style_text_color(pwd_label,lv_color_hex(0x333333),0);
//设置字体大小
     lv_obj_set_style_text_font(pwd_label,NULL,0);
//设置位置，居中，向下偏移200（在键盘上方，避免被键盘遮挡导致点不到）
     lv_obj_align(pwd_label,LV_ALIGN_TOP_MID,0,200);


//2.1创建输入框
   g_pwd_textarea=lv_textarea_create(scr1);
//设置输入框的大小
    lv_obj_set_size(g_pwd_textarea,250,50);
//设置输入框的位置
    lv_obj_align(g_pwd_textarea,LV_ALIGN_TOP_MID,0,225);  // 移到键盘(y>=280)上方，确保整个密码框可点击
//设置输入框的边框大小
    lv_obj_set_style_border_width(g_pwd_textarea,1,0);
//设置 圆角半径 5px
lv_obj_set_style_radius(g_pwd_textarea, 5, 0);   
 // 占位文字（灰色提示）     
lv_textarea_set_placeholder_text(g_pwd_textarea, "input passworld"); 
// 白色背景
lv_obj_set_style_bg_color(g_pwd_textarea, lv_color_hex(0xFFFFFF), 0);   

//设置对话框中密码输入显示隐藏
lv_textarea_set_password_mode(g_pwd_textarea,true);

// ---------------- 4.创建登录按钮 ----------------
    // 在scr1容器下创建按钮，父对象是scr1，按钮属于scr1的子控件
    lv_obj_t *btn1 =lv_obj_create(scr1);

    // 设置按钮控件的尺寸：宽150像素，高80像素
    lv_obj_set_size(btn1,150,80);

    // 设置按钮在父容器scr1内的坐标：x=300，y=320
    lv_obj_set_pos(btn1,300,320);

    // 设置按钮背景颜色，0xADD8E6为淡蓝色
    lv_obj_set_style_bg_color(btn1,lv_color_hex(0xADD8E6),0);


    // ---------------- 5.创建按钮内部标签文字 ----------------
    // lv_label_create(父对象) 创建文本标签控件，父对象为btn1，标签依附在按钮内部
    lv_obj_t *lable2= lv_label_create(btn1);

    // lv_label_set_text(标签对象,字符串)：给标签设置显示的文字内容
    lv_label_set_text(lable2,"login");

    // lv_obj_center(对象)：让该对象在父对象内部自动水平垂直居中
    lv_obj_center(lable2);

    // ----------------6.创建图片并且置顶 ----------------

    // ---------------- 绑定按钮点击回调事件 ----------------
    // lv_obj_add_event_cb(控件,回调函数,事件类型,用户自定义参数)
    // LV_EVENT_CLICKED：鼠标点击松开事件，点击按钮触发btn_to_screen2_cb函数
    // 最后NULL：没有自定义传入参数

        // ========== 创建键盘（用于登录输入） ==========
        // 注意：触摸屏左侧约 152px 是硬件死区，键盘不能占满 800 宽。
        // 收窄到 640 并靠右对齐，确保所有按键都可点（x=160~800 全部在可用区）。
    lv_obj_t *kb = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(kb, 640, 200);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    g_login_kb = kb;
    /* v8.2 无 lv_textarea_set_keyboard()，改用 bind_keyboard()（内部走 lv_keyboard_set_textarea + 聚焦事件） */
    bind_keyboard(g_user_textarea, kb);
    bind_keyboard(g_pwd_textarea, kb);
    /* 键盘按键不应抢占输入框焦点（避免“打字时键盘消失”），并登记 OK(✓) 键收起键盘 */
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(kb, login_kb_ready_cb, LV_EVENT_READY, NULL);

    lv_obj_add_event_cb(btn1,btn_to_screen2_cb,LV_EVENT_CLICKED,NULL);

    // ========== 版本标识（登录页左下角，用于确认部署的是否为新固件） ==========
    lv_obj_t *ver = lv_label_create(scr1);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_text_font(ver, &lv_font_hei16_cjk, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(ver, "V3.1 拼音中文输入版");
}


/**
 * @brief 创建界面2 返回页面
 */
static void create_screen2(void){
    // 创建页面2的容器，挂载到当前屏幕lv_scr_act()
    scr2 = lv_obj_create(lv_scr_act());
    // // 设置登陆界面背景图---也就是scr2
    // 使用新版API：lv_obj_set_style_bg_img_src(对象, 图片, 选择器)
    lv_obj_set_style_bg_img_src(scr2, &chat_bg, 0);

    // 设置页面容器大小铺满屏幕800*480
    lv_obj_set_size(scr2,800,480);

    // 设置容器起始坐标0,0，紧贴屏幕左上角
    lv_obj_set_pos(scr2,0,0);

    // // 设置页面2背景颜色为黄色 0xFFFF00
    // lv_obj_set_style_bg_color(scr2,lv_color_hex(0xFFFF00),0);
    
    

        // ========== 聊天标题 ==========
    lv_obj_t *title = lv_label_create(scr2);//创建文本
    lv_label_set_text(title, "AI Chat");//创建标题
    lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);//设置颜色
    lv_obj_set_style_text_font(title, NULL, 0);  // 使用默认字体
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);  // 顶部居中

    // ---------------- 创建返回按钮 ----------------
    // 在scr2页面容器下创建按钮控件
    lv_obj_t *btn2 =lv_obj_create(scr2);

    // 设置按钮宽40像素，高40像素（缩小一半）
    lv_obj_set_size(btn2,40,40);

    // 设置按钮位置：右上角，留10px边距
    lv_obj_align(btn2, LV_ALIGN_TOP_RIGHT, -10, 10);

    // 设置按钮背景淡蓝色
    lv_obj_set_style_bg_color(btn2,lv_color_hex(0xADD8E6),0);


    // ---------------- 创建按钮内部文字标签 ----------------
    // 创建标签控件，父对象是btn2按钮
    lv_obj_t *lable3= lv_label_create(btn2);

    // 设置标签显示文字 back
    lv_label_set_text(lable3,"back");

    // 文字在按钮内部居中显示
    lv_obj_center(lable3);


    // 给返回按钮绑定点击事件，点击执行btn_to_screen1_cb回调函数
    lv_obj_add_event_cb(btn2,btn_to_screen1_cb,LV_EVENT_CLICKED,NULL);


    //---------------------创建聊天消息容器（用于滚动）-----------------------
    g_chat_container = lv_obj_create(scr2);
    lv_obj_set_size(g_chat_container, 760, 300);   // 宽度760，高度300
    lv_obj_align(g_chat_container, LV_ALIGN_TOP_MID, 0, 65);

    // 设置容器的背景色，让聊天区域可见
    lv_obj_set_style_bg_color(g_chat_container, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(g_chat_container, LV_OPA_100, 0);
    lv_obj_set_style_border_width(g_chat_container, 2, 0);
    lv_obj_set_style_border_color(g_chat_container, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(g_chat_container, 10, 0);

    // 启用滚动
    lv_obj_set_scroll_dir(g_chat_container, LV_DIR_VER);
    lv_obj_add_flag(g_chat_container, LV_OBJ_FLAG_SCROLLABLE);

    // 重置 Y 坐标累加器
    chat_y = 0;

    //===========添加输入框============
    //创建输入框
    // 输入框整体右移进可用区（左侧 152px 是触摸死区），宽 530，右端到 690，给 SEND 留位置
    lv_obj_t *chat_input = lv_textarea_create(scr2);
    //设置输入框大小
    lv_obj_set_size(chat_input, 530, 45);   // 高度改为45
    //设置输入框的位置（x=160 起，避开左侧触摸死区）
    lv_obj_align(chat_input, LV_ALIGN_BOTTOM_LEFT, 160, -10);
    //设置输入框的文本
    lv_textarea_set_placeholder_text(chat_input, "place input...");
    // 聊天输入框也用中文字体，保证输入/回显中文不乱码
    lv_obj_set_style_text_font(chat_input, &lv_font_hei16_cjk, 0);
    //设置边框半径
    lv_obj_set_style_radius(chat_input, 25, 0);
    //设置颜色
    lv_obj_set_style_bg_color(chat_input, lv_color_hex(0xFFFFFF), 0);

    // 保存为全局
    g_chat_input = chat_input;


    //=============聊天发送按钮========

     //创建 发送按钮
    lv_obj_t *send_btn = lv_btn_create(scr2);
    //设置大小
    lv_obj_set_size(send_btn, 90, 45);   // 与输入框高度一致
    //设置右下对其位置
    lv_obj_align(send_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    //设置按钮颜色
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(0x2196F3), 0);
    

    //设置按钮内内容
    lv_obj_t *send_label = lv_label_create(send_btn);
    //设置文本
    lv_label_set_text(send_label, "SEND");
    //设置颜色
    lv_obj_set_style_text_color(send_label, lv_color_hex(0xFFFFFF), 0);
    //设置位置
    lv_obj_center(send_label);

    // 绑定事件（回调函数稍后定义）
    lv_obj_add_event_cb(send_btn, send_btn_cb, LV_EVENT_CLICKED, NULL);
    g_send_btn = send_btn;   // 键盘弹出时要随输入框一起抬升

    // ========== 创建聊天键盘 ==========
    // 同样收窄到 640 并靠右，避开左侧触摸死区
    lv_obj_t *chat_kb = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(chat_kb, 640, 200);
    lv_obj_align(chat_kb, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_flag(chat_kb, LV_OBJ_FLAG_HIDDEN);
    g_chat_kb = chat_kb;
    bind_keyboard(g_chat_input, chat_kb);
    // 键盘"OK/完成"键：收起键盘并发送消息；"x"键：只收起键盘
    lv_obj_add_event_cb(chat_kb, chat_kb_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(chat_kb, chat_kb_cancel_cb, LV_EVENT_CANCEL, NULL);

    // ========== 中文输入：自研拼音输入法（标准 26 键样式；已弃用 v8.3 官方 IME 移植） ==========
    create_pinyin_panel();   // 拼音面板：点"中"按钮开关

    // "中"按钮：切换拼音面板（标题栏右侧，"清"按钮左边）
    lv_obj_t *py_btn = lv_btn_create(scr2);
    lv_obj_set_size(py_btn, 40, 40);
    lv_obj_align(py_btn, LV_ALIGN_TOP_RIGHT, -60, 10);
    lv_obj_set_style_bg_color(py_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_t *py_btn_lb = lv_label_create(py_btn);
    lv_obj_set_style_text_font(py_btn_lb, &lv_font_hei16_cjk, 0);
    lv_obj_set_style_text_color(py_btn_lb, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(py_btn_lb, "中");
    lv_obj_center(py_btn_lb);
    lv_obj_add_event_cb(py_btn, py_toggle_cb, LV_EVENT_CLICKED, NULL);

    // 输入框设为单行：按回车/OK 键直接触发发送
    lv_textarea_set_one_line(g_chat_input, true);

    // "清"按钮：清空对话（标题栏右侧，"中"按钮左边）
    lv_obj_t *clear_btn = lv_btn_create(scr2);
    lv_obj_set_size(clear_btn, 40, 40);
    lv_obj_align(clear_btn, LV_ALIGN_TOP_RIGHT, -110, 10);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0xFF9800), 0);
    lv_obj_t *clear_btn_lb = lv_label_create(clear_btn);
    lv_obj_set_style_text_font(clear_btn_lb, &lv_font_hei16_cjk, 0);
    lv_obj_set_style_text_color(clear_btn_lb, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(clear_btn_lb, "清");
    lv_obj_center(clear_btn_lb);
    lv_obj_add_event_cb(clear_btn, clear_chat_cb, LV_EVENT_CLICKED, NULL);

    // "AI 正在输入..." 提示（聊天区上方，收到回复后隐藏）
    g_thinking = lv_label_create(scr2);
    lv_obj_align(g_thinking, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_text_font(g_thinking, &lv_font_hei16_cjk, 0);
    lv_obj_set_style_text_color(g_thinking, lv_color_hex(0x888888), 0);
    lv_label_set_text(g_thinking, "AI 正在输入...");
    lv_obj_add_flag(g_thinking, LV_OBJ_FLAG_HIDDEN);
}


/**
 * @brief 按钮回调：切换到页面2
 * @param e LVGL事件结构体，保存触发事件的相关信息
 * static 静态函数，仅本文件内部可以调用，外部不可访问
 */
//负责验证登陆密码核对和按钮跳转
static void btn_to_screen2_cb(lv_event_t *e) {
    //1.获取输入框文字
    const char* username=lv_textarea_get_text(g_user_textarea);
    //获取输入框密码
    const char* password=lv_textarea_get_text(g_pwd_textarea);

    //2.验证固定账户(ppl/ppl)——用户已把密码从 123456 改为 ppl
    if(strcmp(username,"ppl")==0&&strcmp(password,"ppl")==0){
        //验证成功
        // lv_obj_add_flag(对象,标志位)：给对象添加隐藏标记 LV_OBJ_FLAG_HIDDEN，控件直接隐藏看不见
    lv_obj_add_flag(scr1, LV_OBJ_FLAG_HIDDEN);

    // 键盘是 lv_scr_act() 的直接子对象，不会跟着 scr1 一起隐藏，必须手动收起
    if(g_login_kb) lv_obj_add_flag(g_login_kb, LV_OBJ_FLAG_HIDDEN);

    // 让输入框失焦，否则键盘状态残留
    if(g_user_textarea) lv_obj_clear_state(g_user_textarea, LV_STATE_FOCUSED);
    if(g_pwd_textarea)  lv_obj_clear_state(g_pwd_textarea, LV_STATE_FOCUSED);

    // 拼音面板也一起收起，避免残留
    if(g_py_panel) lv_obj_add_flag(g_py_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_cand_panel) lv_obj_add_flag(g_cand_panel, LV_OBJ_FLAG_HIDDEN);
    g_py_buf[0] = 0; g_py_n = 0; g_py_open = false;
    chat_kb_layout(false);   // 输入区恢复到底部

    // lv_obj_clear_flag(对象,标志位)：清除隐藏标记，控件恢复显示
    lv_obj_clear_flag(scr2, LV_OBJ_FLAG_HIDDEN);
    }else{
        //验证失败--->清空密码框并且提示
        lv_textarea_set_text(g_pwd_textarea,"");
        // 可以添加一个提示标签，目前先简单处理：修改密码框占位文字
        lv_textarea_set_placeholder_text(g_pwd_textarea, "place tray input");
    }

    

}


/**
 *  按钮回调：切换回到页面1
  e LVGL事件结构体
 */
static void btn_to_screen1_cb(lv_event_t *e) {
    // 隐藏页面2
    lv_obj_add_flag(scr2, LV_OBJ_FLAG_HIDDEN);

    // 聊天键盘同样挂在 lv_scr_act() 上，返回登录页时要手动收起
    if(g_chat_kb) lv_obj_add_flag(g_chat_kb, LV_OBJ_FLAG_HIDDEN);
    if(g_chat_input) lv_obj_clear_state(g_chat_input, LV_STATE_FOCUSED);

    // 拼音面板也收起
    if(g_py_panel) lv_obj_add_flag(g_py_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_cand_panel) lv_obj_add_flag(g_cand_panel, LV_OBJ_FLAG_HIDDEN);
    g_py_buf[0] = 0; g_py_n = 0; g_py_open = false;
    chat_kb_layout(false);   // 输入区恢复到底部

    // 显示页面1
    lv_obj_clear_flag(scr1, LV_OBJ_FLAG_HIDDEN);
}


// 辅助函数：创建一条消息气泡
static void add_message_bubble(const char *text, bool is_user) {
    // 创建标签
    lv_obj_t *bubble = lv_label_create(g_chat_container);
    lv_label_set_text(bubble, text);
    lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(bubble, 300);  // 最大宽度300px
    lv_obj_set_style_text_font(bubble, &lv_font_hei16_cjk, 0);  // 中文回复必须用中文字体，否则渲染成空白
    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_set_style_pad_all(bubble, 8, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_100, 0);  // 设置气泡为不透明

    // 根据角色设置对齐方式和颜色
    if (is_user) {
        // 用户消息：右对齐，绿色背景，白色文字
        lv_obj_align(bubble, LV_ALIGN_TOP_RIGHT, -10, chat_y);
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x4CAF50), 0);
        lv_obj_set_style_text_color(bubble, lv_color_hex(0xFFFFFF), 0);
    } else {
        // AI消息：左对齐，灰色背景，黑色文字
        lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 10, chat_y);
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_text_color(bubble, lv_color_hex(0x000000), 0);
    }

    // 刷新布局以获取实际高度
    lv_obj_update_layout(bubble);
    lv_coord_t h = lv_obj_get_height(bubble);
    // 更新Y坐标（当前气泡高度 + 间距10px）
    chat_y += h + 10;

    // 注意：不要修改容器高度！容器高度已在 create_screen2 中固定
    // lv_obj_set_height(g_chat_container, chat_y + 10); // ← 已删除

    // 滚动到底部，显示最新消息
    lv_obj_scroll_to_y(g_chat_container, LV_COORD_MAX, LV_ANIM_OFF);
}


//关于发送消息按钮后的响应执行操作的函数（已改为异步 AI 调用）
static void send_btn_cb(lv_event_t *e) {
    const char *msg = lv_textarea_get_text(g_chat_input);
    if (strlen(msg) == 0) return;
    if (g_ai_busy) return;   // AI 正在回复，忽略重复发送

    /* ===== 关键：先拷贝！ =====
     * lv_textarea_get_text() 返回的是输入框内部缓冲区的指针。
     * 下面的 lv_textarea_set_text(g_chat_input, "") 会 free 掉这块内存，
     * 之后再用 msg 就是"悬空指针"（use-after-free）：
     *   气泡里能看到文字，但发给 AI 的却是空字符串 ""。
     * 所以必须在清空输入框【之前】把文本复制出来。 */
    char *msg_copy = strdup(msg);

    // 1. 添加用户消息气泡（立即显示）
    add_message_bubble(msg_copy, true);

    // 2. 清空输入框
    lv_textarea_set_text(g_chat_input, "");

    // 3. 将用户消息加入历史
    add_history("user", msg_copy);

    // 4. 标记忙碌并显示"AI 正在输入..."，防止连点重复发送
    g_ai_busy = true;
    if (g_thinking) lv_obj_clear_flag(g_thinking, LV_OBJ_FLAG_HIDDEN);

    // 5. 启动线程获取 AI 回复（异步），msg_copy 的所有权交给线程，线程结束时 free
    pthread_t thread;
    if (pthread_create(&thread, NULL, ai_thread_func, msg_copy) != 0) {
        // 创建线程失败，释放拷贝并显示错误
        free(msg_copy);
        g_ai_busy = false;
        if (g_thinking) lv_obj_add_flag(g_thinking, LV_OBJ_FLAG_HIDDEN);
        add_message_bubble("创建AI线程失败，请重试", false);
    } else {
        pthread_detach(thread);  // 线程结束自动回收
    }
}


// 清空对话：删掉所有气泡、清空输入框、清空 AI 历史记忆、隐藏"正在输入"
static void clear_chat_cb(lv_event_t *e) {
    (void)e;
    if (g_chat_container) {
        lv_obj_clean(g_chat_container);   // 删除容器内所有消息气泡
        chat_y = 0;
    }
    if (g_chat_input) lv_textarea_set_text(g_chat_input, "");
    free_history();                       // 清空 AI 记忆（历史链表）
    g_ai_busy = false;
    if (g_thinking) lv_obj_add_flag(g_thinking, LV_OBJ_FLAG_HIDDEN);
    if (g_cand_panel) lv_obj_add_flag(g_cand_panel, LV_OBJ_FLAG_HIDDEN);
    if (g_py_panel) { g_py_buf[0] = 0; g_py_n = 0; py_refresh(); }
}

/* 页面跳转模块初始化函数
 * 外部调用入口，main.c中lv_init()之后调用
 */void  my_jump_init(void) {
    create_screen1();       // 执行函数，创建页面1所有控件
    create_screen2();       // 执行函数，创建页面2所有控件

    // 启动定时器检查 AI 回复（每 100ms 轮询一次）
    check_timer = lv_timer_create(check_reply_timer, 100, NULL);

    // 默认程序启动的时候隐藏页面2，只显示页面1
    lv_obj_add_flag(scr2, LV_OBJ_FLAG_HIDDEN);
}