# check=skip=FromPlatformFlagConstDisallowed
# APL Othello on the web.
#   docker build -t apl-othello . && docker run --rm -t -p 8080:8080 apl-othello   then open http://localhost:8080
#     (-t: Dyalog holds back its output on a pipe, so without a terminal the log stays empty)
#   docker build --target client-files -o web/static .                              builds only the browser client, for ./web/serve.apls

# The browser client: raylib compiled to WebAssembly, and web/client linked against it.
# The Emscripten image is published for amd64 only; its output runs in any browser, so the platform is fixed.
FROM --platform=linux/amd64 emscripten/emsdk:3.1.74 AS client
ARG RAYLIB=5.5
ARG RAYLIBAPL=325d60971f073405885e8fadde54b1dbaaf06df9
RUN git clone --depth 1 --branch ${RAYLIB} https://github.com/raysan5/raylib /raylib \
 && git init -q /raylibAPL && git -C /raylibAPL fetch -q --depth 1 https://github.com/Brian-ED/raylibAPL ${RAYLIBAPL} \
 && git -C /raylibAPL checkout -q FETCH_HEAD
WORKDIR /raylib/src
RUN emcc -c -Os -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2 rcore.c rshapes.c rtextures.c rtext.c rmodels.c utils.c raudio.c \
 && emar rcs libraylib.a *.o
COPY web/client/main.c /client/main.c
RUN mkdir /out && emcc /client/main.c libraylib.a -I. -Os -DPLATFORM_WEB -sUSE_GLFW=3 -lwebsocket.js \
      -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web '-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$stringToNewUTF8' -o /out/client.js

FROM scratch AS client-files
COPY --from=client /out/ /

# The server: the engine and the UI in Dyalog, serving the client and playing a game per browser
FROM dyalog/dyalog:20.0
WORKDIR /app
COPY src/Othello.apln src/Games.apln src/
COPY ui/App.apln ui/
COPY web/Remote.aplc web/Server.aplc web/serve.apls web/
COPY web/static/index.html web/static/
COPY --from=client /out/ web/static/
COPY --from=client /raylibAPL/link/raylib.apln vendor/raylibAPL/link/
EXPOSE 8080
ENTRYPOINT ["dyalogscript", "/app/web/serve.apls"]
