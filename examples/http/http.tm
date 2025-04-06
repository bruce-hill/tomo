# A simple HTTP library built using CURL

use -lcurl
use <curl/curl.h>

struct HTTPResponse(code:Int, body:Text)

enum _Method(GET, POST, PUT, PATCH, DELETE)

func _send(method:_Method, url:Text, data:Text?, headers:[Text]=[] -> HTTPResponse)
    chunks : @[Text] = @[]
    save_chunk := func(chunk:CString, size:Int64, n:Int64)
        chunks.insert(inline C:Text {
            Text$format("%.*s", _$size*_$n, _$chunk)
        })
        return n*size

    inline C {
        CURL *curl = curl_easy_init();
        struct curl_slist *chunk = NULL;
        curl_easy_setopt(curl, CURLOPT_URL, Text$as_c_string(_$url));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _$save_chunk.fn);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, _$save_chunk.userdata);
    }

    defer
        inline C {
            if (chunk)
                curl_slist_free_all(chunk);
            curl_easy_cleanup(curl);
        }

    when method is POST
        inline C {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        }
        if posting := data
            inline C {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, Text$as_c_string(_$posting));
            }
    is PUT
        inline C {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        }
        if putting := data
            inline C {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, Text$as_c_string(_$putting));
            }
    is PATCH
        inline C {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        }
        if patching := data
            inline C {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, Text$as_c_string(_$patching));
            }
    is DELETE
        inline C {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
    else
        pass

    for header in headers
        inline C {
            chunk = curl_slist_append(chunk, Text$as_c_string(_$header));
        }

    inline C {
        if (chunk)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    code := Int64(0)
    inline C {
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &_$code);
    }
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
