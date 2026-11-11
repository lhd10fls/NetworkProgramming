$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    $includes = 'C:/Program Files/mingw64/opt/include'
    $libraries = 'C:/Program Files/mingw64/opt/lib'
    & gcc -std=c11 -O2 -Wall -Wextra -pthread -D_WIN32_WINNT=0x0601 -I $includes server.c net.c password.c seed_questions.c -L $libraries -lsqlite3 -lws2_32 -lbcrypt -o server.exe
    if ($LASTEXITCODE -ne 0) { throw 'Server compilation failed.' }
    & gcc -std=c11 -O2 -Wall -Wextra -pthread -D_WIN32_WINNT=0x0601 client.c net.c -lws2_32 -o client.exe
    if ($LASTEXITCODE -ne 0) { throw 'Client compilation failed.' }
    Write-Host 'Built server.exe and client.exe'
} finally {
    Pop-Location
}
