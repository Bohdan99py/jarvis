#pragma once
// ===== MEMORY OPTIMIZATION CONFIG =====
// Target: ≤1GB RAM usage
// Phase 1: Conservative limits for stable operation

// ===== SESSION & HISTORY =====
// Reduced from 100 to 20 to save ~40MB
#define MAX_SESSION_MESSAGES 20

// Reduced from 50 to 10 to save ~20MB
#define MAX_PAST_SESSIONS 10

// Reduced from 20 to 10 to save ~5MB
#define MAX_SESSION_TOPICS 10

// Reduced from 20 to 10 to save ~2MB
#define MAX_SESSION_FILES 10

// ===== PROJECT INDEXING =====
// Reduced from 500 to 250 files to save ~50MB
#define MAX_INDEXED_FILES 250

// Reduced from 200 to 100 symbols/file to save ~30MB
#define MAX_SYMBOLS_PER_FILE 100

// ===== VOICE & AUDIO BUFFERS =====
// 5MB max recording buffer
#define MAX_AUDIO_BUFFER_MB 5
#define AUDIO_CHUNK_SIZE 4096

// ===== DATABASE PAGINATION =====
// Fetch messages in smaller chunks
#define MESSAGES_PAGE_SIZE 10
#define QUERY_CHUNK_SIZE 50

// ===== CONTEXT SNAPSHOT =====
// Reduced from 8 to 5 commands
#define MAX_RECENT_COMMANDS 5

// Reduced from 10 to 5 files
#define MAX_RECENT_PROJECT_FILES 5

// ===== CACHING LIMITS =====
// Keep only 50 recent embeddings in memory
#define MAX_CACHED_EMBEDDINGS 50

// Keep only 3 recent screenshots as thumbnails
#define MAX_CACHED_SCREENSHOTS 3

// ===== COMPILATION FEATURE FLAGS =====
// Enable these optimizations by adding to CMakeLists.txt:
// target_compile_definitions(JarvisCore PRIVATE
//     JARVIS_MEMORY_OPTIMIZED
//     USE_DISK_BASED_INDEX
//     ENABLE_PAGINATION
// )
