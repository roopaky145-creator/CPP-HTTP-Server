#pragma once

#include "HttpRequest.h"
#include <string>

// ---------------------------------------------------------------------------
// Handle an incoming HTTP request, mapping the path to the document root,
// checking for traversal attempts, looking up MIME type, and sending
// the file efficiently via sendfile().
// ---------------------------------------------------------------------------
void handle_request(int client_fd, const HttpRequest& req, const std::string& root);
