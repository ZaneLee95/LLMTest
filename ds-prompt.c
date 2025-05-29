#define _GNU_SOURCE // asprintf
#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <json-c/json.h>


// -lcurl -ljson-c -lpcre2-8
// apt install libcurl4-openssl-dev libjson-c-dev libpcre2-dev libpcre2-8-0

#define MAX_TOKENS 2048
#define CONFIDENT_TIMES 3
#define GRAMMAR_RETRIES 5
#define DEEPSEEK_TOKEN "your-api-key"


struct MemoryStruct
{
    char *memory;
    size_t size;
};

static size_t chat_with_llm_helper(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL)
    {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

char *chat_with_llm(char *prompt, int tries, float temperature)
{
    CURL *curl;
    CURLcode res = CURLE_OK;
    char *answer = NULL;
    char *url = "https://api.deepseek.com/v1/chat/completions";
    
    char *auth_header = "Authorization: Bearer " DEEPSEEK_TOKEN;
    char *content_header = "Content-Type: application/json";
    char *accept_header = "Accept: application/json";
    char *prompt_data = NULL;

    // Ensure prompt is properly escaped for JSON
    json_object *jobj = json_object_new_object();
    json_object *messages = json_object_new_array();
    json_object *message = json_object_new_object();
    json_object *content = json_object_new_string(prompt);
    
    json_object_object_add(message, "role", json_object_new_string("user"));
    json_object_object_add(message, "content", content);
    json_object_array_add(messages, message);
    
    json_object_object_add(jobj, "model", json_object_new_string("deepseek-chat"));
    json_object_object_add(jobj, "messages", messages);
    json_object_object_add(jobj, "max_tokens", json_object_new_int(MAX_TOKENS));
    json_object_object_add(jobj, "temperature", json_object_new_double(temperature));
    
    // Convert the JSON object to a string
    const char *json_str = json_object_to_json_string(jobj);
    
    asprintf(&prompt_data, "%s", json_str);
    
    if (prompt_data == NULL) {
        printf("Error: Failed to allocate memory for request data.\n");
        json_object_put(jobj);
        return NULL;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    do
    {
        struct MemoryStruct chunk;
        chunk.memory = malloc(1); /* will be grown as needed by realloc */
        chunk.size = 0;           /* no data at this point */

        curl = curl_easy_init();
        if (curl)
        {
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, auth_header);
            headers = curl_slist_append(headers, content_header);
            headers = curl_slist_append(headers, accept_header);

            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, prompt_data);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, chat_with_llm_helper);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);  // Timeout
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);  // Connection timeout

            res = curl_easy_perform(curl);

            if (res == CURLE_OK)
            {
                json_object *jobj_response = json_tokener_parse(chunk.memory);
                
                // Ensure JSON response is correctly formatted
                if (json_object_object_get_ex(jobj_response, "choices", NULL))
                {
                    json_object *choices = json_object_object_get(jobj_response, "choices");
                    json_object *first_choice = json_object_array_get_idx(choices, 0);
                    json_object *message = json_object_object_get(first_choice, "message");
                    json_object *content = json_object_object_get(message, "content");
                    const char *data = json_object_get_string(content);
                    if (data[0] == '\n') 
                        data++; // Retain original newline handling
                    answer = strdup(data);
                }
                else
                {
                    printf("Error response is: %s\n", chunk.memory);
                    // Output full response for debugging
                    fprintf(stderr, "Full response from Deepseek: %s\n", chunk.memory);
                    sleep(2);
                }
                json_object_put(jobj_response);
            }
            else
            {
                printf("Error: %s\n", curl_easy_strerror(res));
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }

        free(chunk.memory);
    } while ((res != CURLE_OK || answer == NULL) && (--tries > 0));

    curl_global_cleanup();
    return answer;
}

char *construct_prompt_for_templates(char *protocol_name, char **final_msg)
{
    char *prompt_rtsp_example = "对于 RTSP 协议，DESCRIBE 客户端请求模板是：\\n"
                                "DESCRIBE: [\\\"DESCRIBE <<VALUE>>\\\\r\\\\n\\\"," 
                                "\\\"CSeq: <<VALUE>>\\\\r\\\\n\\\"," 
                                "\\\"User-Agent: <<VALUE>>\\\\r\\\\n\\\"," 
                                "\\\"Accept: <<VALUE>>\\\\r\\\\n\\\"," 
                                "\\\"\\\\r\\\\n\\\"]";

    char *prompt_http_example = "对于 HTTP 协议，GET 客户端请求模板是：\\n"
                                "GET: [\\\"GET <<VALUE>>\\\\r\\\\n\\\"]";

    char *msg = NULL;
    asprintf(&msg, "%s\\n%s\\n对于 %s 协议，所有的客户端请求模板如下：", prompt_rtsp_example, prompt_http_example, protocol_name);
    *final_msg = msg;

    char *prompt_grammars = NULL;
    asprintf(&prompt_grammars, "[{\"role\": \"system\", \"content\": \"你是一个有帮助的助手。\"}, {\"role\": \"user\", \"content\": \"%s\"}]", msg);

    return prompt_grammars;
}

int main()
{
    char *protocol_name = "RTSP";  // 修正协议名称为字符串
    char *first_question;
    char *prompt = construct_prompt_for_templates(protocol_name, &first_question);
    
    printf("Got prompt:\n\n%s\n",prompt);
    
    char *response =  chat_with_llm(prompt, GRAMMAR_RETRIES, 0.5);

    if (response != NULL) {
        FILE *file = fopen("ds-response.txt", "w");
        
        if (file)
        {
            // 将接收到的数据写入文件
            fwrite(response, 1, strlen(response), file);  // 写入返回的响应
            fclose(file); // 关闭文件
            printf("Response saved to ds-response.txt\n");
        } 
        else
        {
            fprintf(stderr, "Failed to open file for writing.\n");
        }

        free(response);  // 释放返回的内存
    } else {
        printf("No response received.\n");
    }

    return 0;
}
