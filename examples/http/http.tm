# A simple HTTP library built using CURL

use -lcurl
use <curl/curl.h>

struct HTTPResponse(code:Int, body:Text)

enum _Method(GET, POST, PUT, PATCH, DELETE)

func _send(method:_Method, url:Text, data:Text?, headers:[Text]=[] -> HTTPResponse)
    chunks : @[Text]
    save_chunk := func(chunk:CString, size:Int64, n:Int64)
        chunks.insert(C_code:Text`Text$from_strn(@chunk, @size*@n)`)
        return n*size

    C_code `
        CURL *curl = curl_easy_init();
        struct curl_slist *chunk = NULL;
        curl_easy_setopt(curl, CURLOPT_URL, @(CString(url)));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, @save_chunk.fn);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, @save_chunk.userdata);
    `

    defer
        C_code `
            if (chunk)
                curl_slist_free_all(chunk);
            curl_easy_cleanup(curl);
        `

    when method is POST
        C_code `
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        `
        if posting := data
            C_code `
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, @(CString(posting)));
            `
    is PUT
        C_code `
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        `
        if putting := data
            C_code `
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, @(CString(putting)));
            `
    is PATCH
        C_code `
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        `
        if patching := data
            C_code `
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, @(CString(patching)));
            `
    is DELETE
        C_code `
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        `
    else
        pass

    for header in headers
        C_code `
            chunk = curl_slist_append(chunk, @(CString(header)));
        `

    C_code `
        if (chunk)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    `

    code := Int64(0)
    C_code ```
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &@code);
    ```
    return HTTPResponse(Int(code), "".join(chunks))

func get(url:Text, headers:[Text]=[] -> HTTPResponse)
    return _send(GET, url, none, headers)

func post(url:Text, data="", headers=["Content-Type: application/json", "Accept: application/json"] -> HTTPResponse)
    return _send(POST, url, data, headers)

func put(url:Text, data="", headers=["Content-Type: application/json", "Accept: application/json"] -> HTTPResponse)
    return _send(PUT, url, data, headers)

func patch(url:Text, data="", headers=["Content-Type: application/json", "Accept: application/json"] -> HTTPResponse)
    return _send(PATCH, url, data, headers)

func delete(url:Text, data:Text?=none, headers=["Content-Type: application/json", "Accept: application/json"] -> HTTPResponse)
    return _send(DELETE, url, data, headers)

func main()
    say("GET:")
    say(get("https://httpbin.org/get").body)
    say("Waiting 1sec...")
    sleep(1)
    say("POST:")
    say(post("https://httpbin.org/post", `{"key": "value"}`).body)
    say("Waiting 1sec...")
    sleep(1)
    say("PUT:")
    say(put("https://httpbin.org/put", `{"key": "value"}`).body)
    say("Waiting 1sec...")
    sleep(1)
    say("DELETE:")
    say(delete("https://httpbin.org/delete").body)
