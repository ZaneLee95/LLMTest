#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// 用于存储从服务器接收到的数据结构体
struct MemoryStruct {
  char *memory;
  size_t size;
};

// libcurl 写数据的回调函数
// 当 libcurl 接收到数据时，会调用此函数
static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb; // 计算接收到的实际数据大小
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  // 动态扩展内存以存储新数据
  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* out of memory! */
    fprintf(stderr, "错误：内存不足 (realloc 返回 NULL)\n");
    return 0; // 返回0表示错误
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize); // 拷贝新数据到内存末尾
  mem->size += realsize; // 更新已存储数据的大小
  mem->memory[mem->size] = 0; // 添加字符串结束符

  return realsize; // 返回成功处理的数据大小
}

int main(void) {
  CURL *curl_handle;
  CURLcode res;

  struct MemoryStruct chunk;
  chunk.memory = malloc(1);  /* 将会根据需要通过 realloc 扩展 */
  chunk.size = 0;            /* 初始化时没有数据 */

  const char *api_key = "your-gemini-api-key";

  // ---------------------------------------------------------------------------
  // 重要：请将 "http://your_proxy_address:port" 替换为你的真实代理服务器地址和端口
  // 例如："http://proxy.example.com:8080" 或 "socks5://localhost:1080"
  // 如果你的代理不需要端口，或者有其他协议 (如 SOCKS5)，请相应修改。
  // ---------------------------------------------------------------------------
  const char *proxy_url = "http://192.168.81.1:7890"; // <--- 在这里配置你的代理
  // ---------------------------------------------------------------------------

  const char *model_name = "gemini-1.5-flash-latest"; // 你也可以尝试 "gemini-1.5-flash-latest" 等其他模型
  char url[512];
  sprintf(url, "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", model_name);

  const char *prompt_text = "你好，Gemini！请用中文介绍一下你自己，确认我们可以通信。";
  char json_payload[1024];
  sprintf(json_payload, "{\"contents\":[{\"parts\":[{\"text\":\"%s\"}]}]}", prompt_text);

  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();

  if(curl_handle) {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char api_key_header[256];
    sprintf(api_key_header, "x-goog-api-key: %s", api_key);
    headers = curl_slist_append(headers, api_key_header);

    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, (long)strlen(json_payload));
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

    // ***********************************************************************
    // ** 添加代理设置 **
    // 如果 proxy_url 不是默认的占位符，并且长度大于0，则设置代理
    if (proxy_url && strlen(proxy_url) > 0 && strcmp(proxy_url, "http://your_proxy_address:port") != 0) {
        curl_easy_setopt(curl_handle, CURLOPT_PROXY, proxy_url);
        printf("信息：已配置使用代理服务器: %s\n", proxy_url);

        // 如果你的代理服务器需要认证 (用户名和密码)
        // 取消下面这行的注释，并替换 "user:password"
        // curl_easy_setopt(curl_handle, CURLOPT_PROXYUSERPWD, "user:password");
    }
    // ***********************************************************************

    // 可选：开启详细输出，用于调试网络请求过程
    // curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

    res = curl_easy_perform(curl_handle);

    if(res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() 执行失败: %s\n", curl_easy_strerror(res));
    } else {
      printf("成功接收到来自 Gemini 的响应。\n");
      printf("原始 JSON 响应内容:\n%s\n", chunk.memory);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl_handle);
    free(chunk.memory);
  } else {
    fprintf(stderr, "curl_easy_init() 初始化失败\n");
    free(chunk.memory);
  }

  curl_global_cleanup();
  return 0;
}

