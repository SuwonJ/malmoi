#include "malmoi_bridge.h"
#include <stdlib.h>
#include <string.h>

#define NONE ((size_t)-1)
#define SAME_SCORE 0.995f
#define HOP_PENALTY 0.05

typedef struct {
    const char *word, *key;   /* 문자열 포인터만 저장, 복사하지 않음 */
    double dist;              /* 출발점부터의 누적 거리 */
    size_t steps;             /* 출발점부터의 칸 수 */
    size_t prev;              /* 이전 노드 번호, 출발점은 NONE */
    float prev_score;         /* 이전 노드와의 유사도 */
    int done;                 /* 거리 확정 여부 */
} Node;

typedef struct {
    Node *nodes; size_t count, capacity;  /* 노드 동적 배열 */
    size_t *table; size_t table_size;     /* 해시 테이블: 칸마다 노드 번호 또는 NONE */
} Graph;

void malmoi_bridge_default_options(MalmoiBridgeOptions *options) {
    if (!options) return;
    options->neighbors = 10;
    options->max_steps = 6;
    options->max_visits = 500;
}

/* 입력 단어에서 '-'와 '^'를 제거한 새 문자열. 호출자가 free 해야 함 */
static char *make_key(const char *word) {
    size_t n = strlen(word), i, j = 0;
    char *key = (char *)malloc(n + 1);
    if (!key) return NULL;
    for (i = 0; i < n; ++i)
        if (word[i] != '-' && word[i] != '^') key[j++] = word[i];
    key[j] = '\0';
    return key;
}

/* 문자열을 큰 숫자 하나로 바꿔요 (FNV-1a 해시).
 * 같은 문자열은 항상 같은 숫자가 나와서, 해시 테이블의 칸 번호를 정할 때 써요. */
static size_t hash_text(const char *s) {
    size_t h = (size_t)2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* 노드 배열 16칸, 해시 테이블 32칸으로 그래프를 처음 준비해요.
 * 해시 테이블의 모든 칸은 "비어 있음(NONE)"으로 채워요. */
static int graph_init(Graph *g) {
    size_t i;
    g->count = 0;
    g->capacity = 16;
    g->table_size = 32;
    g->nodes = (Node *)malloc(g->capacity * sizeof(Node));
    g->table = (size_t *)malloc(g->table_size * sizeof(size_t));
    if (!g->nodes || !g->table) {
        return MALMOI_MEMORY;
    }
    for (i = 0; i < g->table_size; ++i) {
        g->table[i] = NONE;
    }
    return MALMOI_OK;
}

/* key가 이미 그래프에 있으면 노드 번호를, 없으면 NONE을 돌려줘요.
 * 해시 값으로 시작 칸을 정하고, 칸이 차 있는데 다른 단어면 다음 칸을 봐요(개방 주소법).
 * 빈 칸을 만나면 그 단어는 없는 거예요. */
static size_t find_node(const Graph *g, const char *key) {
    size_t i = hash_text(key) % g->table_size;
    while (g->table[i] != NONE) {
        if (strcmp(g->nodes[g->table[i]].key, key) == 0) {
            return g->table[i];
        }
        i = (i + 1) % g->table_size;
    }
    return NONE;
}

/* 노드 번호 id를 해시 테이블의 빈 칸에 넣어요. */
static void put_node(Graph *g, size_t id) {
    size_t i = hash_text(g->nodes[id].key) % g->table_size;
    while (g->table[i] != NONE) {
        i = (i + 1) % g->table_size;
    }
    g->table[i] = id;
}

/* 해시 테이블을 두 배 크기로 새로 만들고 모든 노드를 다시 넣어요.
 * 크기가 바뀌면 "해시 값 % 크기"도 바뀌어서, 옛 칸 위치를 그대로 쓸 수 없어요. */
static int grow_table(Graph *g) {
    size_t new_size = g->table_size * 2;
    size_t *new_table = (size_t *)malloc(new_size * sizeof(size_t));
    size_t i;
    if (!new_table) {
        return MALMOI_MEMORY;
    }
    for (i = 0; i < new_size; ++i) {
        new_table[i] = NONE;
    }
    free(g->table);
    g->table = new_table;
    g->table_size = new_size;
    for (i = 0; i < g->count; ++i) {
        put_node(g, i);
    }
    return MALMOI_OK;
}

/* 새 단어를 노드 배열 맨 뒤에 추가하고, 해시 테이블에도 등록해요.
 * 성공하면 *out에 새 노드 번호를 넣어요. */
static int add_node(Graph *g, const char *word, const char *key, size_t *out) {
    Node *node;
    int rc;

    /* 1. 노드 배열이 꽉 찼으면 두 배로 늘려요.
     *    realloc 뒤에는 배열 주소가 바뀔 수 있어서, 노드는 항상 번호로 다뤘요. */
    if (g->count == g->capacity) {
        size_t new_capacity = g->capacity * 2;
        Node *bigger = (Node *)realloc(g->nodes, new_capacity * sizeof(Node));
        if (!bigger) {
            return MALMOI_MEMORY;
        }
        g->nodes = bigger;
        g->capacity = new_capacity;
    }

    /* 2. 해시 테이블이 70% 넘게 차면 두 배로 키워요.
     *    너무 꽉 차면 빈 칸을 찾느라 오래 걸려요. */
    if ((g->count + 1) * 10 > g->table_size * 7) {
        rc = grow_table(g);
        if (rc) {
            return rc;
        }
    }

    /* 3. 새 노드를 "아직 모르는 단어" 상태로 채워요. */
    node = &g->nodes[g->count];
    node->word = word;
    node->key = key;
    node->dist = 1e30;          /* 아직 길을 모르니 아주 먼 거리 */
    node->steps = 0;
    node->prev = NONE;          /* 어디서 왔는지 아직 없음 */
    node->prev_score = 0.0f;
    node->done = 0;             /* 아직 확정 안 됨 */

    /* 4. 해시 테이블에 등록하고 번호를 돌려줘요. */
    put_node(g, g->count);
    *out = g->count;
    g->count++;
    return MALMOI_OK;
}

int malmoi_bridge(const Malmoi *engine, const char *from, const char *to,
                  const MalmoiBridgeOptions *options,
                  MalmoiStep *out, size_t capacity, size_t *count) {
    MalmoiBridgeOptions defaults;   /* options가 NULL일 때 쓸 기본값 */
    Graph graph = {0};              /* 노드 배열 + 해시 테이블 (D-2, D-3) */
    MalmoiResult *neighbors = NULL; /* malmoi_similar 결과를 받을 배열 */
    char *from_key = NULL;          /* '-', '^'를 뺨 출발 단어 */
    char *to_key = NULL;            /* '-', '^'를 뺨 도착 단어 */
    size_t start;                   /* 출발 노드 번호 */
    size_t target = NONE;           /* 도착 노드 번호 (찾으면 채움) */
    size_t visits = 0;              /* 지금까지 확정한 단어 수 */
    size_t i;
    float pair_score;
    int rc;

    /* ===== 1. 인자 검사 ===== */
    if (!count) {
        return MALMOI_ARGUMENT;
    }
    *count = 0;
    if (!engine || !from || !to) {
        return MALMOI_ARGUMENT;
    }
    if (!options) {
        malmoi_bridge_default_options(&defaults);
        options = &defaults;
    }

    /* ===== 2. 두 단어가 사전에 있는지 확인 =====
     * 없는 단어면 MALMOI_NOT_FOUND 같은 오류가 그대로 나와요. */
    rc = malmoi_similarity(engine, from, to, &pair_score);
    if (rc != MALMOI_OK) {
        return rc;
    }

    /* ===== 3. 준비 ===== */
    from_key = make_key(from);
    to_key = make_key(to);
    neighbors = (MalmoiResult *)malloc((options->neighbors ? options->neighbors : 1)
                                       * sizeof(MalmoiResult));
    if (!from_key || !to_key || !neighbors) {
        rc = MALMOI_MEMORY;
        goto done;
    }
    rc = graph_init(&graph);
    if (rc != MALMOI_OK) {
        goto done;
    }
    rc = add_node(&graph, from, from_key, &start);
    if (rc != MALMOI_OK) {
        goto done;
    }
    graph.nodes[start].dist = 0.0;   /* 출발점까지의 거리는 0 */

    /* ===== 4. 다익스트라 반복 ===== */
    for (;;) {
        size_t u = NONE;          /* 이번 차례에 꺼낼 노드 번호 */
        double best = 1e30;
        size_t n = 0, j;

        /* 4-a. 아직 확정 안 된 노드 중 거리가 가장 짧은 것 고르기 */
        for (i = 0; i < graph.count; ++i) {
            if (!graph.nodes[i].done && graph.nodes[i].dist < best) {
                best = graph.nodes[i].dist;
                u = i;
            }
        }
        if (u == NONE) {
            /* 더 갈 곳이 없음 = 연결 없음. 오류는 아니라서 OK + count 0 */
            rc = MALMOI_OK;
            goto done;
        }

        /* 4-b. 꺼낸 단어가 도착 단어면 끝 (꺼낼 때 확정!) */
        if (strcmp(graph.nodes[u].key, to_key) == 0) {
            target = u;
            break;
        }

        /* 4-c. 확정 표시, 탐색 한도 확인 */
        graph.nodes[u].done = 1;
        visits++;
        if (visits > options->max_visits) {
            rc = MALMOI_LIMIT;
            goto done;
        }

        /* 4-d. 최대 칸 수에 도달했으면 더 뻗어 나가지 않음 */
        if (graph.nodes[u].steps >= options->max_steps) {
            continue;
        }

        /* 4-e. 이 단어의 이웃(유사어 상위 neighbors개) 받아 오기 */
        rc = malmoi_similar(engine, graph.nodes[u].key, neighbors, options->neighbors, &n);
        if (rc != MALMOI_OK) {
            goto done;
        }

        /* 4-f. 이웃마다 "u를 거쳐 가면 더 가까운가?" 확인 */
        for (j = 0; j < n; ++j) {
            size_t v;
            double weight, new_dist;

            /* 뜻풀이가 똑같은 복제 연결은 건너뛰기 (예: 비-감 1.000) */
            if (neighbors[j].score >= SAME_SCORE) {
                continue;
            }
            /* 길 길이 = 1 - 유사도 + 한 칸 벌점 (항상 0 이상) */
            weight = 1.0 - neighbors[j].score + HOP_PENALTY;

            /* 처음 보는 단어면 노드로 추가. 주의: add_node 뒤에는
             * graph.nodes 주소가 바뀔 수 있으니 포인터를 들고 있지 않아요. */
            v = find_node(&graph, neighbors[j].key);
            if (v == NONE) {
                rc = add_node(&graph, neighbors[j].word, neighbors[j].key, &v);
                if (rc != MALMOI_OK) {
                    goto done;
                }
            }

            new_dist = graph.nodes[u].dist + weight;
            if (!graph.nodes[v].done && new_dist < graph.nodes[v].dist) {
                graph.nodes[v].dist = new_dist;
                graph.nodes[v].steps = graph.nodes[u].steps + 1;
                graph.nodes[v].prev = u;                 /* 어디서 왔는지 메모 */
                graph.nodes[v].prev_score = neighbors[j].score;
            }
        }
    }

    /* ===== 5. 경로 되짚기 (D-5) =====
     * 도착 노드부터 prev를 따라가며 out을 뒤에서부터 채워요. */
    {
        size_t length = graph.nodes[target].steps + 1;
        size_t x = target;
        if (length > capacity || !out) {
            rc = MALMOI_RANGE;
            goto done;
        }
        for (i = length - 1; i > 0; --i) {
            out[i].word = graph.nodes[x].word;
            out[i].key = graph.nodes[x].key;
            out[i].score = graph.nodes[x].prev_score;
            x = graph.nodes[x].prev;
        }
        /* 출발 칸: from_key는 아래에서 free하므로 호출자가 준 from을 넣어요 */
        out[0].word = from;
        out[0].key = from;
        out[0].score = 1.0f;
        *count = length;
        rc = MALMOI_OK;
    }

done:
    /* ===== 6. 정리 (D-6) =====
     * 어떤 경로로 끝나든 여기서 모두 해제해요. free(NULL)은 안전해요. */
    free(graph.nodes);
    free(graph.table);
    free(neighbors);
    free(from_key);
    free(to_key);
    return rc;
}