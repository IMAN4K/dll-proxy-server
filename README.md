# dll-proxy-server
This small project is a HTTP/HTTPS forward proxy server utility which is capable for running inside shared libraries (.dll/.so) without a process entry point.

## Get started
To run the server follow based on platform:
 * Linux:
   * `LD_PRELOAD=libProxyServer.so ls`
 * Win32:
   * `rundll32 ProxyServer.DLL,start`
