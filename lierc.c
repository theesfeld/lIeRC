#include <curl/curl.h>
#include <json-c/json.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>
#define MAX_THREADS 10

pthread_t thread_pool[MAX_THREADS];
int thread_pool_size = 0;
pthread_mutex_t thread_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t thread_pool_cond = PTHREAD_COND_INITIALIZER;

// Simple queue implementation
typedef struct QueueNode {
  void *data;
  struct QueueNode *next;
} QueueNode;

typedef struct {
  QueueNode *front;
  QueueNode *rear;
} queue_t;

queue_t *queue_create() {
  queue_t *q = malloc(sizeof(queue_t));
  q->front = q->rear = NULL;
  return q;
}

void queue_destroy(queue_t *q) {
  while (q->front != NULL) {
    QueueNode *temp = q->front;
    q->front = q->front->next;
    free(temp);
  }
  free(q);
}

FILE *log_file = NULL;
pthread_mutex_t chat_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t bot_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

queue_t *response_queue;

// Helper function to escape special characters in JSON strings
void json_escape_string(const char *input, char *output, size_t output_size) {
  size_t i, j;
  for (i = 0, j = 0; input[i] != '\0' && j < output_size - 1; i++) {
    switch (input[i]) {
    case '\"':
      if (j + 2 < output_size - 1) {
        output[j++] = '\\';
        output[j++] = '\"';
      }
      break;
    case '\\':
      if (j + 2 < output_size - 1) {
        output[j++] = '\\';
        output[j++] = '\\';
      }
      break;
    case '\b':
      if (j + 2 < output_size - 1) {
        output[j++] = '\\';
        output[j++] = 'b';
      }
      break;
    case '\f':
      if (j + 2 < output_size - 1) {
        output[j++] = '\\';
        output[j++] = 'f';
      }
      break;
    case '\n':
      if (j + 2 < output_size - 1) {
        output[j++] = '\\';
        output[j++] = 'n';
      }
      break;
    case '\r':
      if (j + 2 < output_size - 1) {
        output[j++] = '\\';
        output[j++] = 'r';
      }
      break;
    case '\t':
      if (j + 2 < output_size - 1) {
        output[j++] = '\\';
        output[j++] = 't';
      }
      break;
    default:
      output[j++] = input[i];
      break;
    }
  }
  output[j] = '\0';
}

#define MAX_RESPONSE_SIZE 4096
#define MAX_QUERY_SIZE 256
#define CHAT_HISTORY_LIMIT 100
#define DEFAULT_MODEL "gpt-4"
#define MAX_BOTS 10

// Color pair indices
#define COLOR_TIME 1
#define COLOR_USERNAME_USER 2
#define COLOR_USERNAME_ASSISTANT 3
#define COLOR_BRACKETS 4
#define COLOR_STATUS_BAR 5
#define COLOR_TEXT 6
#define COLOR_SIDEBAR 7
#define COLOR_SYSTEM_MESSAGE 8
#define COLOR_LLM_MESSAGE 9
#define COLOR_TYPING 10

// Chat message structure
typedef struct {
  char timestamp[20];    // Timestamp in [HH:mm] format
  char role[16];         // "user" or "assistant"
  char display_name[50]; // Display name like "gpt-4" for the assistant
  char content[MAX_RESPONSE_SIZE]; // The message content
} ChatMessage;

// Bot structure
#define MAX_MEMORY_ENTRIES 10
#define MAX_MEMORY_ENTRY_LENGTH 200

typedef struct {
  char api_type[20];     // "openai" or "anthropic"
  char name[50];         // Display name of the bot
  char personality[256]; // Personality description
  float temperature;     // Temperature for API calls
  char memory[MAX_MEMORY_ENTRIES]
             [MAX_MEMORY_ENTRY_LENGTH]; // Circular buffer for recent
                                        // interactions
  int memory_index;                     // Current index in the circular buffer
  int memory_count;                     // Number of entries in the memory
  int total_messages;  // Total number of messages processed by the bot
  pthread_t thread_id; // Thread ID for the bot's autonomous behavior
  int is_active;       // Flag to indicate if the bot is active
  int is_typing;       // Flag to indicate if the bot is currently "typing"
} Bot;

// Global variables
char *openai_api_key;
char *anthropic_api_key;
char model[50];
char user_name[50] = "user"; // Default user name

// Chat history buffer
ChatMessage chat_history[CHAT_HISTORY_LIMIT];
int chat_index = 0;      // Tracks the number of chat messages in history
int scroll_position = 0; // Tracks the scroll position

// Bot list
Bot bots[MAX_BOTS];
int bot_count = 0;

// Stats
int messages_sent = 0;
int messages_received = 0;
time_t start_time;

// ncurses windows
WINDOW *chat_win, *input_win, *status_win, *sidebar_win;

// Structure to hold response from the OpenAI API
struct memory {
  char *response;
  size_t size;
};

// Function to handle memory reallocation for the CURL response
static size_t write_callback(void *data, size_t size, size_t nmemb,
                             void *userp) {
  size_t realsize = size * nmemb;
  struct memory *mem = (struct memory *)userp;

  char *ptr = realloc(mem->response, mem->size + realsize + 1);
  if (ptr == NULL) {
    return 0;
  }

  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, realsize);
  mem->size += realsize;
  mem->response[mem->size] = 0;

  return realsize;
}

// Get timestamp in [HH:mm] format
void get_timestamp(char *buffer, size_t buffer_size) {
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  snprintf(buffer, buffer_size, "[%02d:%02d]", tm.tm_hour, tm.tm_min);
}

// Update the status bar with connection time and message stats
void update_status_bar() {
  werase(status_win);
  int max_x;
  getmaxyx(status_win, (int){0}, max_x);

  // Set blue background for the status bar and extend across the entire width
  wattron(status_win, COLOR_PAIR(COLOR_STATUS_BAR));
  for (int i = 0; i < max_x; i++) {
    mvwprintw(status_win, 0, i, " ");
  }

  // Time connected
  time_t now = time(NULL);
  int seconds_connected = (int)difftime(now, start_time);
  int minutes = seconds_connected / 60;
  int seconds = seconds_connected % 60;

  mvwprintw(status_win, 0, 0,
            " Time Connected: %02d:%02d  |  Sent: %d  |  Received: %d ",
            minutes, seconds, messages_sent, messages_received);

  wattroff(status_win, COLOR_PAIR(COLOR_STATUS_BAR));
  wrefresh(status_win);
}

// Word-wrap function to ensure clean text breaks on spaces
void word_wrap(char *input, char *output, int max_width) {
  int len = strlen(input);
  int space_left = max_width;
  int out_pos = 0;
  for (int i = 0; i < len; i++) {
    if (input[i] == ' ' && i > 0) {
      output[out_pos++] = input[i];
      space_left = max_width;
    } else if (space_left == 0) {
      output[out_pos++] = '\n';
      output[out_pos++] = input[i];
      space_left = max_width - 1;
    } else {
      output[out_pos++] = input[i];
      space_left--;
    }
  }
  output[out_pos] = '\0'; // Null terminate the output
}

// Function to update and render the chat window with messages
void update_chat_window() {
  werase(chat_win); // Clear the chat window first
  int max_y, max_x;
  getmaxyx(chat_win, max_y, max_x); // Get the dimensions of the chat window

  int line = 0; // Track the current line number in the chat window

  // Iterate through the chat history and render messages, taking scrolling into
  // account
  for (int i = 0; i < CHAT_HISTORY_LIMIT; i++) {
    int idx =
        (scroll_position + i) % CHAT_HISTORY_LIMIT; // Circular buffer handling
    if (strlen(chat_history[idx].content) == 0) {
      continue; // Skip empty chat history slots
    }

    char formatted_message[MAX_RESPONSE_SIZE +
                           100]; // Buffer for formatted message

    // Prepare the message format: [timestamp] <username>: message
    snprintf(formatted_message, sizeof(formatted_message), "[%s] <%s>: %s",
             chat_history[idx].timestamp, chat_history[idx].display_name,
             chat_history[idx].content);

    // Now handle word wrapping to ensure text doesn't overflow the window width
    int message_length = strlen(formatted_message);
    int start_pos = 0;

    while (message_length > 0 && line < max_y) {
      int chars_to_print =
          (message_length < max_x) ? message_length : max_x - 1;
      mvwprintw(chat_win, line++, 0, "%.*s", chars_to_print,
                formatted_message + start_pos);
      message_length -= chars_to_print;
      start_pos += chars_to_print;
    }

    // Stop rendering if we've run out of space in the chat window
    if (line >= max_y) {
      break;
    }
  }

  // Refresh the chat window to apply changes
  wrefresh(chat_win);
}

// Scroll the chat window up
void scroll_up() {
  if (scroll_position > 0) {
    scroll_position--;
    update_chat_window();
  }
}

// Scroll the chat window down
void scroll_down() {
  int max_y;
  getmaxyx(chat_win, max_y, (int){0});
  if (scroll_position < chat_index - max_y) {
    scroll_position++;
    update_chat_window();
  }
}

// Function to add chat messages to the chat window and ensure they're displayed
// properly
void add_chat_message(const char *role, const char *display_name,
                      const char *message) {
  pthread_mutex_lock(&chat_mutex);

  get_timestamp(chat_history[chat_index].timestamp,
                sizeof(chat_history[chat_index].timestamp));
  strncpy(chat_history[chat_index].role, role,
          sizeof(chat_history[chat_index].role));
  strncpy(chat_history[chat_index].display_name, display_name,
          sizeof(chat_history[chat_index].display_name));
  strncpy(chat_history[chat_index].content, message, MAX_RESPONSE_SIZE);

  chat_index++;
  if (chat_index >= CHAT_HISTORY_LIMIT) {
    chat_index = 0; // Scroll the buffer
  }

  scroll_position = chat_index; // Automatically scroll to the latest message
  update_chat_window();         // Refresh chat window to show new messages

  // Log the message to file if logging is enabled
  if (log_file != NULL) {
    fprintf(log_file, "[%s] <%s> %s\n", chat_history[chat_index - 1].timestamp,
            display_name, message);
    fflush(log_file);
  }

  pthread_mutex_unlock(&chat_mutex);
}

// Function to log errors and display them in the chat window
void log_error(const char *error_message) {
  add_chat_message("system", "system", error_message);
}

// Function to generate a unique bot personality using OpenAI
char *
generate_unique_bot_personality(float *temperature,
                                char existing_personalities[MAX_BOTS][256],
                                int bot_count) {
  CURL *curl;
  CURLcode res;
  struct memory chunk = {0};
  chunk.response = malloc(1);
  chunk.size = 0;

  curl = curl_easy_init();
  if (!curl) {
    log_error("CURL initialization failed.");
    return NULL;
  }

  // Set up the request to OpenAI
  const char *url = "https://api.openai.com/v1/chat/completions";
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  char auth_header[256];
  snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
           openai_api_key);
  headers = curl_slist_append(headers, auth_header);

  // Create a string of existing personalities
  char existing_personalities_str[MAX_BOTS * 256] = "";
  for (int i = 0; i < bot_count; i++) {
    strcat(existing_personalities_str, existing_personalities[i]);
    strcat(existing_personalities_str, " ");
  }

  // Create the JSON request body
  char json_data[4096];
  int json_data_size = sizeof(json_data);
  int written = snprintf(
      json_data, json_data_size,
      "{\"model\": \"gpt-3.5-turbo\", \"messages\": [{\"role\": \"system\", "
      "\"content\": \"Generate a short, one-sentence personality description "
      "for a chatbot inspired by various online communities. Include a mix of "
      "traits such as helpful, sarcastic, meme-loving, intellectual, "
      "optimistic, cynical, or quirky. Aim for diversity in personalities. "
      "Ensure the personality is unique and different from these existing "
      "personalities: %.1000s\"}], "
      "\"max_tokens\": 50}",
      existing_personalities_str);

  if (written >= json_data_size) {
    log_error("JSON data truncated in generate_unique_bot_personality");
  }

  // Set up CURL options
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

  // Perform the request
  res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    log_error(curl_easy_strerror(res));
    free(chunk.response);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return NULL;
  }

  // Parse the JSON response
  struct json_object *parsed_json;
  struct json_object *choices_array;
  struct json_object *message_object;
  struct json_object *message_content;

  parsed_json = json_tokener_parse(chunk.response);
  char *personality = NULL;

  if (json_object_object_get_ex(parsed_json, "choices", &choices_array)) {
    if (json_object_get_type(choices_array) == json_type_array) {
      message_object = json_object_array_get_idx(choices_array, 0);
      if (json_object_object_get_ex(message_object, "message",
                                    &message_object)) {
        if (json_object_object_get_ex(message_object, "content",
                                      &message_content)) {
          const char *response_text = json_object_get_string(message_content);
          personality = strdup(response_text);
        }
      }
    }
  }

  // Generate a random temperature between 0.5 and 1.0 for more varied responses
  *temperature = ((float)rand() / RAND_MAX) * 0.5 + 0.5;

  // Clean up
  json_object_put(parsed_json);
  free(chunk.response);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  return personality;
}

// Function to handle /whois command
void handle_whois(const char *bot_name) {
  for (int i = 0; i < bot_count; i++) {
    if (strcmp(bots[i].name, bot_name) == 0) {
      char info[1024];
      snprintf(info, sizeof(info),
               "Bot Information:\n"
               "Name: %s\n"
               "API Type: %s\n"
               "Temperature: %.2f\n"
               "Personality: %s\n"
               "Total Messages: %d\n"
               "Memory Size: %d/%d\n",
               bots[i].name, bots[i].api_type, bots[i].temperature,
               bots[i].personality, bots[i].total_messages,
               bots[i].memory_count, MAX_MEMORY_ENTRIES);
      add_chat_message("system", "system", info);
      return;
    }
  }
  log_error("Bot not found.");
}

// Function prototype for process_bot_responses
void *process_bot_responses(void *arg);

// Function to generate a bot personality using OpenAI
char *generate_bot_personality(float *temperature) {
  CURL *curl;
  CURLcode res;
  struct memory chunk = {0};
  chunk.response = malloc(1);
  chunk.size = 0;

  curl = curl_easy_init();
  if (!curl) {
    log_error("CURL initialization failed.");
    return NULL;
  }

  // Set up the request to OpenAI
  const char *url = "https://api.openai.com/v1/chat/completions";
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  char auth_header[256];
  snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
           openai_api_key);
  headers = curl_slist_append(headers, auth_header);

  // Create the JSON request body
  char json_data[1024];
  snprintf(
      json_data, sizeof(json_data),
      "{\"model\": \"gpt-3.5-turbo\", \"messages\": [{\"role\": \"system\", "
      "\"content\": \"Generate a short, one-sentence personality description "
      "for a chatbot inspired by various online communities. Include a mix of "
      "traits such as helpful, sarcastic, meme-loving, intellectual, "
      "optimistic, "
      "cynical, or quirky. Aim for diversity in personalities.\"}], "
      "\"max_tokens\": 50}");

  // Set up CURL options
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

  // Perform the request
  res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    log_error(curl_easy_strerror(res));
    free(chunk.response);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return NULL;
  }

  // Parse the JSON response
  struct json_object *parsed_json;
  struct json_object *choices_array;
  struct json_object *message_object;
  struct json_object *message_content;

  parsed_json = json_tokener_parse(chunk.response);
  char *personality = NULL;

  if (json_object_object_get_ex(parsed_json, "choices", &choices_array)) {
    if (json_object_get_type(choices_array) == json_type_array) {
      message_object = json_object_array_get_idx(choices_array, 0);
      if (json_object_object_get_ex(message_object, "message",
                                    &message_object)) {
        if (json_object_object_get_ex(message_object, "content",
                                      &message_content)) {
          const char *response_text = json_object_get_string(message_content);
          personality = strdup(response_text);
        }
      }
    }
  }

  // Generate a random temperature between 0.5 and 1.0 for more varied responses
  *temperature = ((float)rand() / RAND_MAX) * 0.5 + 0.5;

  // Clean up
  json_object_put(parsed_json);
  free(chunk.response);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  return personality;
}

// Function prototype for bot_autonomous_behavior
void *bot_autonomous_behavior(void *arg);

// Function to display the sidebar with connected bots
char *find_matching_username(const char *partial) {
  if (strncmp(partial, user_name, strlen(partial)) == 0) {
    return user_name;
  }
  for (int i = 0; i < bot_count; i++) {
    if (strncmp(partial, bots[i].name, strlen(partial)) == 0) {
      return bots[i].name;
    }
  }
  return NULL;
}

void update_sidebar() {
  werase(sidebar_win);
  mvwprintw(sidebar_win, 1, 2, "Users:");
  mvwprintw(sidebar_win, 2, 2, "@%s", user_name);
  for (int i = 0; i < bot_count; i++) {
    if (bots[i].is_typing) {
      wattron(sidebar_win, COLOR_PAIR(COLOR_TYPING));
    } else {
      wattron(sidebar_win, COLOR_PAIR(COLOR_SIDEBAR));
    }
    mvwprintw(sidebar_win, i + 3, 2, "+%s [%s]", bots[i].name,
              bots[i].api_type);
    if (bots[i].is_typing) {
      wattroff(sidebar_win, COLOR_PAIR(COLOR_TYPING));
    } else {
      wattroff(sidebar_win, COLOR_PAIR(COLOR_SIDEBAR));
    }
  }
  wrefresh(sidebar_win);
}

// Parse and handle slash commands like adding or removing a bot
void handle_command(char *input) {
  char command[MAX_QUERY_SIZE];
  sscanf(input, "%s", command);

  if (strcmp(command, "/addbot") == 0) {
    char api_type[20], name[50];
    if (sscanf(input, "/addbot %s %s", api_type, name) == 2) {
      if (bot_count < MAX_BOTS) {
        Bot *new_bot = &bots[bot_count];
        strncpy(new_bot->api_type, api_type, sizeof(new_bot->api_type));
        strncpy(new_bot->name, name, sizeof(new_bot->name));

        // Generate unique personality and temperature
        float temperature;
        char existing_personalities[MAX_BOTS][256];
        for (int i = 0; i < bot_count; i++) {
          strncpy(existing_personalities[i], bots[i].personality, 256);
        }
        char *personality = generate_unique_bot_personality(
            &temperature, existing_personalities, bot_count);
        if (personality) {
          strncpy(new_bot->personality, personality,
                  sizeof(new_bot->personality));
          free(personality);
        } else {
          strncpy(new_bot->personality, "Default personality",
                  sizeof(new_bot->personality));
        }
        new_bot->temperature = temperature;

        // Initialize memory and counters
        memset(new_bot->memory, 0, sizeof(new_bot->memory));
        new_bot->memory_index = 0;
        new_bot->memory_count = 0;
        new_bot->total_messages = 0;

        bot_count++;
        new_bot->is_active = 1;
        pthread_create(&new_bot->thread_id, NULL, bot_autonomous_behavior,
                       new_bot);
        update_sidebar();
        char success_message[256];
        snprintf(success_message, sizeof(success_message), "Added %s bot '%s'",
                 api_type, name);
        add_chat_message("system", "system", success_message);
      } else {
        log_error("Bot limit reached.");
      }
    } else {
      log_error(
          "Invalid command format. Usage: /addbot <openai|anthropic> <name>");
    }
  } else if (strcmp(command, "/kick") == 0) {
    char botname[50];
    if (sscanf(input, "/kick %s", botname) == 1) {
      if (strcmp(botname, "all") == 0) {
        bot_count = 0;
        update_sidebar();
        add_chat_message("system", "system", "All bots kicked.");
      } else {
        int found = 0;
        for (int i = 0; i < bot_count; i++) {
          if (strcmp(bots[i].name, botname) == 0) {
            found = 1;
            bots[i].is_active = 0;
            pthread_join(bots[i].thread_id, NULL);
            for (int j = i; j < bot_count - 1; j++) {
              bots[j] = bots[j + 1]; // Shift all bots left
            }
            bot_count--;
            update_sidebar();
            add_chat_message("system", "system", "Bot kicked.");
            break;
          }
        }
        if (!found) {
          log_error("Bot not found.");
        }
      }
    } else {
      log_error("Invalid command format. Usage: /kick <botname|all>");
    }
  } else if (strcmp(command, "/nick") == 0) {
    char new_nick[50];
    if (sscanf(input, "/nick %s", new_nick) == 1) {
      strncpy(user_name, new_nick, sizeof(user_name));
      update_sidebar();
      add_chat_message("system", "system", "User name changed.");
    } else {
      log_error("Invalid command format. Usage: /nick <newname>");
    }
  } else if (strcmp(command, "/quit") == 0) {
    endwin();
    exit(0);
  } else if (strcmp(command, "/whois") == 0) {
    char botname[50];
    if (sscanf(input, "/whois %s", botname) == 1) {
      handle_whois(botname);
    } else {
      log_error("Invalid command format. Usage: /whois <botname>");
    }
  } else {
    log_error("Unknown command.");
  }
}

// Function to determine if a bot should respond
int should_bot_respond(const char *message, const char *bot_personality,
                       const char *bot_memory, const char *bot_name,
                       int is_mentioned) {
  CURL *curl;
  CURLcode res;
  struct memory chunk = {0};
  chunk.response = malloc(1);
  chunk.size = 0;

  // If the bot is mentioned, it should respond with very high probability
  if (is_mentioned) {
    free(chunk.response);
    return (rand() % 100) < 95; // 95% chance to respond when mentioned
  }

  // Random chance to respond even when not mentioned
  if ((rand() % 100) < 30) { // 30% chance to consider responding
    free(chunk.response);
    return 1;
  }

  curl = curl_easy_init();
  if (!curl) {
    log_error("CURL initialization failed in should_bot_respond.");
    return 0;
  }

  // Set up the request to OpenAI
  const char *url = "https://api.openai.com/v1/chat/completions";
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  char auth_header[256];
  snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
           openai_api_key);
  headers = curl_slist_append(headers, auth_header);

  // Create the JSON request body
  char json_data[4096];
  snprintf(json_data, sizeof(json_data),
           "{\"model\": \"gpt-3.5-turbo\", \"messages\": ["
           "{\"role\": \"system\", \"content\": \"You are an AI assistant that "
           "determines if a bot with a given personality should respond to a "
           "message. The bot's name is %s. "
           "The bot's personality is: %s. The bot's recent memory is: %s. "
           "Consider the context and the bot's personality. Respond with only "
           "'yes' if the bot should respond, "
           "or 'no' if it shouldn't. Aim for natural conversation flow and "
           "avoid having the bot respond to every message.\"}, "
           "{\"role\": \"user\", \"content\": \"Should the bot respond to this "
           "message: %s\"}], "
           "\"max_tokens\": 1, \"temperature\": 0.7}",
           bot_name, bot_personality, bot_memory, message);

  // Set up CURL options
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

  // Perform the request
  res = curl_easy_perform(curl);
  int should_respond = 0;

  if (res != CURLE_OK) {
    log_error(curl_easy_strerror(res));
  } else {
    // Parse the JSON response
    struct json_object *parsed_json;
    struct json_object *choices_array;
    struct json_object *message_object;
    struct json_object *message_content;

    parsed_json = json_tokener_parse(chunk.response);

    if (json_object_object_get_ex(parsed_json, "choices", &choices_array)) {
      if (json_object_get_type(choices_array) == json_type_array) {
        message_object = json_object_array_get_idx(choices_array, 0);
        if (json_object_object_get_ex(message_object, "message",
                                      &message_object)) {
          if (json_object_object_get_ex(message_object, "content",
                                        &message_content)) {
            const char *response_text = json_object_get_string(message_content);
            should_respond = (strcasecmp(response_text, "yes") == 0);
          }
        }
      }
    }

    json_object_put(parsed_json);
  }

  // Clean up
  free(chunk.response);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  return should_respond;
}

// Function to update bot's memory
void update_bot_memory(Bot *bot, const char *new_interaction) {
  pthread_mutex_lock(&bot_mutex);

  // Create a summary of the new interaction
  char summary[MAX_MEMORY_ENTRY_LENGTH];
  snprintf(summary, sizeof(summary), "Last interaction: %.100s...",
           new_interaction);

  // Add summary to the circular buffer
  strncpy(bot->memory[bot->memory_index], summary, MAX_MEMORY_ENTRY_LENGTH - 1);
  bot->memory[bot->memory_index][MAX_MEMORY_ENTRY_LENGTH - 1] = '\0';

  // Update index and count
  bot->memory_index = (bot->memory_index + 1) % MAX_MEMORY_ENTRIES;
  if (bot->memory_count < MAX_MEMORY_ENTRIES) {
    bot->memory_count++;
  }

  // Increment total messages
  bot->total_messages++;

  // Create a consolidated memory string
  char consolidated_memory[MAX_MEMORY_ENTRIES * MAX_MEMORY_ENTRY_LENGTH];
  consolidated_memory[0] = '\0';
  for (int i = 0; i < bot->memory_count; i++) {
    strcat(consolidated_memory, bot->memory[i]);
    strcat(consolidated_memory, " ");
  }

  // Update the first memory entry with the consolidated memory
  strncpy(bot->memory[0], consolidated_memory, MAX_MEMORY_ENTRY_LENGTH - 1);
  bot->memory[0][MAX_MEMORY_ENTRY_LENGTH - 1] = '\0';

  pthread_mutex_unlock(&bot_mutex);
}

// Function to check if a message mentions a specific user
int is_mentioned(const char *message, const char *username) {
  char mention[52]; // @username
  snprintf(mention, sizeof(mention), "@%s", username);
  return strstr(message, mention) != NULL;
}

// Structure to pass data to the bot response thread
typedef struct {
  char *query;
  char *sender;
  Bot *bot;
} BotThreadData;

// Function for the bot response thread
void *bot_response_thread(void *arg) {
  BotThreadData *data = (BotThreadData *)arg;
  const char *query = data->query;
  const char *sender = data->sender;
  Bot *bot = data->bot;

  // Set typing status
  bot->is_typing = 1;
  update_sidebar();

  CURL *curl;
  CURLcode res;
  struct memory chunk = {0};
  chunk.response = malloc(1);
  chunk.size = 0;

  curl = curl_easy_init();
  if (!curl) {
    log_error("CURL initialization failed in bot thread.");
    bot->is_typing = 0;
    update_sidebar();
    free(data);
    return NULL;
  }

  // Create the correct URL and headers for each bot type
  char url[256];
  struct curl_slist *headers = NULL;

  if (strcmp(bot->api_type, "openai") == 0) {
    // OpenAI URL and headers
    strcpy(url, "https://api.openai.com/v1/chat/completions");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
             openai_api_key);
    headers = curl_slist_append(headers, auth_header);
  } else if (strcmp(bot->api_type, "anthropic") == 0) {
    // Anthropic URL and headers
    strcpy(url, "https://api.anthropic.com/v1/complete");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
             anthropic_api_key);
    headers = curl_slist_append(headers, auth_header);
  } else {
    log_error("Unknown bot API type.");
    curl_easy_cleanup(curl);
    free(data);
    return NULL;
  }

  // Create a string with the last few messages for context
  char context[MAX_QUERY_SIZE * 5] = "";
  int context_messages = 5; // Number of previous messages to include
  int start_index =
      (chat_index - context_messages < 0) ? 0 : chat_index - context_messages;
  for (int j = start_index; j < chat_index; j++) {
    char temp[MAX_QUERY_SIZE * 2];
    int temp_size = sizeof(temp);
    int written =
        snprintf(temp, temp_size, "%.50s: %.1000s\n",
                 chat_history[j].display_name, chat_history[j].content);
    if (written >= temp_size) {
      log_error("Message truncated in update_chat_window");
    }
    strncat(context, temp, sizeof(context) - strlen(context) - 1);
  }

  // Create the JSON request body
  char
      json_data[MAX_QUERY_SIZE * 20]; // Further increased size for more context
  char escaped_personality[MAX_QUERY_SIZE * 2];
  char escaped_memory[MAX_QUERY_SIZE * 2];
  char escaped_context[MAX_QUERY_SIZE * 5];
  char escaped_query[MAX_QUERY_SIZE * 2];

  // Escape special characters in strings
  json_escape_string(bot->personality, escaped_personality,
                     sizeof(escaped_personality));
  json_escape_string(bot->memory[0], escaped_memory, sizeof(escaped_memory));
  json_escape_string(context, escaped_context, sizeof(escaped_context));
  json_escape_string(query, escaped_query, sizeof(escaped_query));

  int is_bot_mentioned = is_mentioned(query, bot->name);

  int json_data_size = sizeof(json_data);
  int written = snprintf(
      json_data, json_data_size,
      "{\"model\": \"%.50s\", \"messages\": ["
      "{\"role\": \"system\", \"content\": \"You are a chatbot named %.50s "
      "with the following personality: %.200s. Respond in a way that "
      "reflects this personality. Be sarcastic, make jokes, and poke fun at "
      "the user or other bots when appropriate. Don't be overly helpful or "
      "polite. "
      "Your responses should be reminiscent of IRC, Discord, or Reddit "
      "conversations. "
      "Occasionally, initiate new topics or ask questions to keep the "
      "conversation going. "
      "The message you're responding to was sent by %.50s. "
      "Your recent memory is: %.200s. "
      "Here's the recent conversation context:\\n%.1000s "
      "You %s directly mentioned in this message. "
      "If the conversation seems to be dying down, introduce a new topic or "
      "ask a question.\"},"
      "{\"role\": \"user\", \"content\": \"%.200s\"}"
      "], \"temperature\": %.2f, \"stream\": false}",
      model, bot->name, escaped_personality, sender, escaped_memory,
      escaped_context, is_bot_mentioned ? "were" : "were not", escaped_query,
      bot->temperature);

  if (written >= json_data_size) {
    log_error("JSON data truncated in bot_response_thread");
  }

  // Set up CURL options
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

  // Perform the request and handle response
  res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    log_error(curl_easy_strerror(res));
  } else {
    // Parse the JSON response and extract the bot's response
    struct json_object *parsed_json;
    struct json_object *choices_array;
    struct json_object *message_object;
    struct json_object *message_content;

    parsed_json = json_tokener_parse(chunk.response);

    if (parsed_json == NULL) {
      log_error("Failed to parse JSON response");
      add_chat_message("system", "system", "Failed to parse JSON response");
    } else {
      const char *response_text = NULL;

      if (strcmp(bot->api_type, "openai") == 0) {
        // OpenAI API response parsing
        if (json_object_object_get_ex(parsed_json, "choices", &choices_array)) {
          if (json_object_get_type(choices_array) == json_type_array) {
            message_object = json_object_array_get_idx(choices_array, 0);
            if (json_object_object_get_ex(message_object, "message",
                                          &message_object)) {
              if (json_object_object_get_ex(message_object, "content",
                                            &message_content)) {
                response_text = json_object_get_string(message_content);
              }
            }
          }
        }
      } else if (strcmp(bot->api_type, "anthropic") == 0) {
        // Anthropic API response parsing
        if (json_object_object_get_ex(parsed_json, "completion",
                                      &message_content)) {
          response_text = json_object_get_string(message_content);
        }
      }

      if (response_text != NULL) {
        // Add the bot's response to the queue
        pthread_mutex_lock(&queue_mutex);
        QueueNode *new_node = malloc(sizeof(QueueNode));
        BotThreadData *response_data = malloc(sizeof(BotThreadData));
        response_data->query = strdup(response_text);
        response_data->sender = strdup(bot->name);
        response_data->bot = bot;
        new_node->data = response_data;
        new_node->next = NULL;

        if (response_queue->rear == NULL) {
          response_queue->front = response_queue->rear = new_node;
        } else {
          response_queue->rear->next = new_node;
          response_queue->rear = new_node;
        }

        pthread_cond_signal(&queue_cond);
        pthread_mutex_unlock(&queue_mutex);
      } else {
        char error_message[1024];
        snprintf(error_message, sizeof(error_message),
                 "Failed to extract response from JSON. Raw response: %.900s",
                 chunk.response);
        log_error(error_message);
        add_chat_message("system", "system", error_message);
      }

      json_object_put(parsed_json); // Free the parsed JSON object
    }
  }

  // Cleanup CURL resources
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  free(chunk.response);
  free(data);
  bot->is_typing = 0;
  update_sidebar();
  return NULL;
}

// Function to send a query to the OpenAI or Anthropic API based on bot type
void send_chat_query(const char *query, const char *sender) {
  for (int i = 0; i < bot_count; i++) {
    // Skip if the bot is responding to its own message
    if (strcmp(sender, bots[i].name) == 0) {
      continue;
    }

    int is_bot_mentioned = is_mentioned(query, bots[i].name);
    if (!should_bot_respond(query, bots[i].personality, bots[i].memory[0],
                            bots[i].name, is_bot_mentioned)) {
      continue; // Skip this bot if it shouldn't respond
    }

    // Add a random delay between 1 and 3 seconds before responding
    sleep(1 + (rand() % 3));

    // Prepare thread data
    BotThreadData *thread_data = malloc(sizeof(BotThreadData));
    thread_data->query = strdup(query);
    thread_data->sender = strdup(sender);
    thread_data->bot = &bots[i];

    // Add task to thread pool
    pthread_mutex_lock(&thread_pool_mutex);
    while (thread_pool_size >= MAX_THREADS) {
      pthread_cond_wait(&thread_pool_cond, &thread_pool_mutex);
    }

    if (pthread_create(&thread_pool[thread_pool_size], NULL,
                       bot_response_thread, thread_data) != 0) {
      log_error("Failed to create thread for bot response");
      free(thread_data->query);
      free(thread_data->sender);
      free(thread_data);
    } else {
      thread_pool_size++;
    }
    pthread_mutex_unlock(&thread_pool_mutex);
  }
}

// Process user input and display responses
void *process_user_input(void *arg) {
  (void)arg;
  char query[MAX_QUERY_SIZE];
  int ch, cursor_pos = 0;

  while (1) {
    wmove(input_win, 0, 0);
    wclrtoeol(input_win);
    mvwprintw(input_win, 0, 0, "%s", query);
    wmove(input_win, 0, cursor_pos);
    wrefresh(input_win);

    ch = wgetch(input_win);

    if (ch == '\n') {
      if (query[0] == '/') {
        handle_command(query);
      } else {
        add_chat_message("user", user_name, query);
        messages_sent++;
        update_status_bar();

        send_chat_query(query, user_name);

        for (int i = 0; i < bot_count; i++) {
          int is_bot_mentioned = is_mentioned(query, bots[i].name);
          if (should_bot_respond(query, bots[i].personality, bots[i].memory[0],
                                 bots[i].name, is_bot_mentioned)) {
            send_chat_query(chat_history[chat_index - 1].content, bots[i].name);
          }
        }
      }
      memset(query, 0, MAX_QUERY_SIZE);
      cursor_pos = 0;
    } else if (ch == KEY_BACKSPACE || ch == 127) {
      if (cursor_pos > 0) {
        memmove(&query[cursor_pos - 1], &query[cursor_pos],
                strlen(query) - cursor_pos + 1);
        cursor_pos--;
      }
    } else if (ch == '\t') {
      // Handle autocomplete
      int at_pos = cursor_pos - 1;
      while (at_pos >= 0 && query[at_pos] != '@') {
        at_pos--;
      }
      if (at_pos >= 0) {
        char partial[MAX_QUERY_SIZE];
        strncpy(partial, &query[at_pos + 1], cursor_pos - at_pos - 1);
        partial[cursor_pos - at_pos - 1] = '\0';
        char *match = find_matching_username(partial);
        if (match) {
          int match_len = strlen(match);
          memmove(&query[at_pos + match_len + 1], &query[cursor_pos],
                  strlen(query) - cursor_pos + 1);
          memcpy(&query[at_pos + 1], match, match_len);
          cursor_pos = at_pos + match_len + 1;
        }
      }
    } else if (cursor_pos < MAX_QUERY_SIZE - 1) {
      memmove(&query[cursor_pos + 1], &query[cursor_pos],
              strlen(query) - cursor_pos + 1);
      query[cursor_pos] = ch;
      cursor_pos++;
    }
  }
}

// Initialize ncurses
void init_ncurses() {
  initscr();
  start_color();
  keypad(stdscr, TRUE); // Enable keypad input

  // Define color pairs
  init_pair(COLOR_TIME, COLOR_BLUE, COLOR_BLACK);
  init_pair(COLOR_USERNAME_USER, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_USERNAME_ASSISTANT, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_BRACKETS, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_STATUS_BAR, COLOR_WHITE,
            COLOR_BLUE); // Status bar has a blue background
  init_pair(COLOR_TEXT, COLOR_WHITE, COLOR_BLACK);
  init_pair(COLOR_SIDEBAR, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_SYSTEM_MESSAGE, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(COLOR_LLM_MESSAGE, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_TYPING, COLOR_BLACK, COLOR_YELLOW);

  noecho();
  cbreak();

  int height, width;
  getmaxyx(stdscr, height, width);

  // Create windows for different sections
  sidebar_win = newwin(height - 2, 20, 0, 0); // Sidebar for connected users
  chat_win = newwin(height - 3, width - 20, 0, 20); // Chat window
  status_win = newwin(1, width - 20, height - 2,
                      20); // Status bar (one line above input)
  input_win = newwin(1, width - 20, height - 1, 20); // Input box

  update_sidebar();
  update_chat_window();
  update_status_bar();
}

// Function to set up logging
void setup_logging(const char *log_filename) {
  if (log_filename == NULL) {
    // Generate default log filename
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char default_filename[64];
    strftime(default_filename, sizeof(default_filename), "%Y%m%d_%H%M%S.log",
             t);
    log_file = fopen(default_filename, "w");
  } else {
    log_file = fopen(log_filename, "w");
  }

  if (log_file == NULL) {
    fprintf(stderr, "Error: Unable to open log file.\n");
  }
}

// Function for autonomous bot behavior
void *bot_autonomous_behavior(void *arg) {
  Bot *bot = (Bot *)arg;
  while (bot->is_active) {
    // Sleep for a random interval between 5 and 30 seconds
    sleep(5 + (rand() % 26));

    // Decide if the bot wants to speak
    if (rand() % 100 < 30) { // 30% chance to speak
      // Generate a message based on the bot's personality and recent memory
      char message[MAX_QUERY_SIZE * 2];
      int message_size = sizeof(message);
      int written = snprintf(
          message, message_size,
          "As %.50s, I want to say something based on my personality: %.200s",
          bot->name, bot->personality);
      if (written >= message_size) {
        log_error("Message truncated in bot_autonomous_behavior");
      }

      // Send the generated message to the chat
      send_chat_query(message, bot->name);
    }
  }
  return NULL;
}

// Function to process bot responses
void *process_bot_responses(void *arg) {
  (void)arg;
  while (1) {
    pthread_mutex_lock(&queue_mutex);
    while (response_queue->front == NULL) {
      pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    // Get the response from the queue
    QueueNode *node = response_queue->front;
    BotThreadData *data = (BotThreadData *)node->data;
    response_queue->front = node->next;
    if (response_queue->front == NULL) {
      response_queue->rear = NULL;
    }
    pthread_mutex_unlock(&queue_mutex);

    // Process the response
    add_chat_message("assistant", data->bot->name, data->query);
    messages_received++;
    update_status_bar();
    update_bot_memory(data->bot, data->query);

    // Clean up
    free(data->query);
    free(data->sender);
    free(data);
    free(node);
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  strcpy(model, DEFAULT_MODEL);
  const char *log_filename = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--model") == 0 || strcmp(argv[i], "-m") == 0) {
      if (i + 1 < argc) {
        strncpy(model, argv[i + 1], 50);
        i++;
      }
    } else if (strcmp(argv[i], "--log") == 0 || strcmp(argv[i], "-l") == 0) {
      if (i + 1 < argc) {
        log_filename = argv[i + 1];
        i++;
      }
    }
  }

  setup_logging(log_filename);

  openai_api_key = getenv("OPENAI_API_KEY");
  anthropic_api_key = getenv("ANTHROPIC_API_KEY");

  if (openai_api_key == NULL || anthropic_api_key == NULL) {
    fprintf(stderr, "Error: API keys not set in environment variables.\n");
    return 1;
  }

  init_ncurses();
  start_time = time(NULL);

  // Initialize the response queue
  response_queue = queue_create();

  // Create threads for user input and bot responses
  pthread_t user_input_thread, bot_response_thread;
  pthread_create(&user_input_thread, NULL, process_user_input, NULL);
  pthread_create(&bot_response_thread, NULL, process_bot_responses, NULL);

  // Wait for user input thread to finish (which it never will in this case)
  pthread_join(user_input_thread, NULL);

  // Clean up bot threads
  for (int i = 0; i < bot_count; i++) {
    bots[i].is_active = 0;
    pthread_join(bots[i].thread_id, NULL);
  }

  // Clean up bot response thread
  pthread_cancel(bot_response_thread);
  pthread_join(bot_response_thread, NULL);

  // Clean up resources
  queue_destroy(response_queue);
  endwin();
}
