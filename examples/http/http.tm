# A simple HTTP library built using CURL

use libcurl.so
use <curl/curl.h>

struct HTTPResponse(code:Int, body:Text)

enum _Method(GET, POST, PUT, PATCH, DELETE)

_curl := !@Memory

func _send(method:_Method, url:Text, data:Text?, headers=[:Text])->HTTPResponse:
    chunks := @[:Text]
    save_chunk := func(chunk:CString, size:Int64, n:Int64):
        chunks:insert(inline C:Text {
            Text$format("%.*s", $size*$n, $chunk)
        })
        return n*size

    inline C {
        CURL *curl = curl_easy_init();
        struct curl_slist *chunk = NULL;
        curl_easy_setopt(curl, CURLOPT_URL, Text$as_c_string($url));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, $save_chunk.fn);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, $save_chunk.userdata);
    }

    when method is POST:
        inline C {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        }
        if posting := data:
            inline C {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, Text$as_c_string($posting));
            }
    is PUT:
        inline C {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        }
        if putting := data:
            inline C {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, Text$as_c_string($putting));
            }
    is DELETE:
        inline C {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
    else:
        pass

    for header in headers:
        inline C {
            chunk = curl_slist_append(chunk, Text$as_c_string($header));
        }

    inline C {
        if (chunk)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    code := 0[64]
    inline C {
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &$code);
        if (chunk)
            curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }
    return HTTPResponse(code, "":join(chunks))

func get(url:Text, headers=[:Text])->HTTPResponse:
    return _send(GET, url, !Text, headers)

func post(url:Text, data="", headers=["Content-Type: application/json", "Accept: application/json"])->HTTPResponse:
    return _send(POST, url, data, headers)

func put(url:Text, data="", headers=["Content-Type: application/json", "Accept: application/json"])->HTTPResponse:
    return _send(PUT, url, data, headers)

func delete(url:Text, data=!Text, headers=["Content-Type: application/json", "Accept: application/json"])->HTTPResponse:
    return _send(DELETE, url, data, headers)

func main():
    !! GET:
    say(get("https://httpbin.org/get").body)
    !! Waiting 1sec...
    sleep(1)
    !! POST:
    say(post("https://httpbin.org/post", `{"key": "value"}`).body)
    !! Waiting 1sec...
    sleep(1)
    !! PUT:
    say(put("https://httpbin.org/put", `{"key": "value"}`).body)
    !! Waiting 1sec...
    sleep(1)
    !! DELETE:
    say(delete("https://httpbin.org/delete").body)
