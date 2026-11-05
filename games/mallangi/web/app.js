const $ = id => document.getElementById(id);
// sessionStorage survives a reload but ordinary new tabs have separate storage.
const SESSION_KEY = 'mallangi.session.v1';
let sessionId = '';
let busy = false;
let finished = true;
let submitted = [];
let roomCode = null;
let roomPolling = false;

async function request(path, data) {
    const response = await fetch(path, {
        method: 'POST', headers: {
            'Content-Type': 'application/json',
            'X-Mallangi-Session': sessionId
        },
        body: JSON.stringify(data)
    });
    const result = await response.json();
    if (!response.ok || result.error) {
        const error = new Error(result.error || '서버 오류');
        error.status = response.status;
        throw error;
    }
    if (result.sessionId) {
        sessionId = result.sessionId;
        saveSession(sessionId);
    }
    return result;
}

function feedback(message, error = false) {
    $('feedback').textContent = message;
    $('feedback').classList.toggle('error', error);
}

function lock(value) {
    busy = value;
    $('guessInput').disabled = value || finished;
    $('submitButton').disabled = value || finished;
    $('restartButton').disabled = value;
    $('resultRestartButton').disabled = value;
    $('createRoomButton').disabled = value;
    $('joinRoomButton').disabled = value;
    $('saveNameButton').disabled = value;
    $('recoverButton').disabled = value;
}

function render(state) {
    submitted = state.history || [];
    if (roomCode !== state.roomCode) {
        $('roomPlayers').replaceChildren();
        $('roomResultSummary').textContent = '';
        $('roomSyncStatus').textContent = state.roomCode ? '참가자 목록을 불러오는 중…' : '';
    }
    roomCode = state.roomCode;
    $('roomPlayersPanel').hidden = !roomCode;
    $('roomStatus').textContent = state.roomCode
        ? `참가 중인 방: ${state.roomCode} · 최대 8명`
        : '현재 혼자 플레이 중입니다.';
    $('restartButton').textContent = state.roomCode ? '방 나가기 · 혼자 새로 시작' : '혼자 다시 시작';
    $('resultRestartButton').textContent = state.roomCode ? '방 나가기 · 혼자 새로 시작' : '다시 도전하기';
    finished = state.finished;
    $('cards').replaceChildren();
    state.cards.forEach((card, index) => {
        const article = document.createElement('article');
        article.className = 'word-card';
        article.innerHTML = '<div class="card-number"></div><div class="card-word"></div><div class="card-bottom"><span class="score-label">유사도</span><span class="score"></span></div>';
        article.querySelector('.card-number').textContent = `CARD 0${index + 1}`;
        article.querySelector('.card-word').textContent = card.word;
        article.querySelector('.score').textContent = card.score.toFixed(2);
        $('cards').append(article);
    });
    $('attempts').replaceChildren();
    for (let i = 0; i < 5; i++) {
        const dot = document.createElement('span');
        dot.className = 'attempt-dot' + (i >= state.remaining ? ' used' : '');
        $('attempts').append(dot);
    }
    $('attempts').setAttribute('aria-label', `남은 제출 기회 ${state.remaining}회`);
    $('historyCount').textContent = `${submitted.length} / 5`;
    $('historyList').replaceChildren();
    submitted.forEach((item, i) => {
        const row = document.createElement('li');
        row.className = 'history-item';
        for (const [className, text] of [
            ['history-rank', String(i + 1).padStart(2, '0')],
            ['history-word', item.word],
            ['history-score', item.status === 'correct' ? '정답' : item.score.toFixed(2)]
        ]) {
            const span = document.createElement('span');
            span.className = className;
            span.textContent = text;
            row.append(span);
        }
        $('historyList').append(row);
    });
    if (!submitted.length) {
        const empty = document.createElement('li');
        empty.className = 'empty';
        empty.textContent = '아직 제출한 단어가 없습니다.';
        $('historyList').append(empty);
    }
    $('gameStatus').textContent = finished ? `게임 완료 · ${state.won ? '승리' : '패배'}` : '게임 진행 중';
    $('gameStatus').classList.toggle('finished', finished);
    $('resultPanel').hidden = !finished;
    if (finished) {
        $('resultPanel').classList.toggle('lost', !state.won);
        $('resultIcon').textContent = state.won ? '✓' : '!';
        $('resultTitle').textContent = state.won ? '정답을 찾았습니다!' : '기회를 모두 사용했어요.';
        $('resultText').textContent = `오늘의 단어는 '${state.answer}'입니다.`;
        $('resultPanel').focus();
    }
}

async function start(path = '/api/start', data = {}) {
    if (busy) return;
    if (path === '/api/rooms/create' || path === '/api/rooms/join') {
        const name = $('nicknameInput').value.trim();
        if (name) {
            if (!validName(name)) return;
            data = { ...data, name };
        }
    }
    lock(true);
    try {
        const state = await request(path, data);
        $('guessInput').value = '';
        render(state);
        feedback('카드와 유사도는 실제 데이터셋에서 가져왔습니다.');
    } catch (error) {
        feedback(`시작 실패: ${error.message}`, true);
    } finally {
        lock(false);
        refreshRoom();
    }
}

$('guessForm').addEventListener('submit', async event => {
    event.preventDefault();
    if (busy || finished || event.isComposing) return;
    const word = $('guessInput').value.trim().normalize('NFC');
    if (!word || new TextEncoder().encode(word).length >= 64) {
        feedback('1~63바이트의 단어를 입력해주세요. 한글은 보통 한 글자에 3바이트입니다.', true);
        return;
    }
    lock(true);
    try {
        const state = await request('/api/guess', { word });
        render(state);
        const messages = {
            correct: '정답입니다!', wrong: `오답입니다. 유사도 ${state.score.toFixed(2)}`,
            duplicate: '이미 제출한 단어입니다. 기회는 유지됩니다.',
            unknown: '등록되지 않았거나 유효한 벡터가 없는 단어입니다. 기회는 유지됩니다.',
            invalid: '올바른 단어를 입력해주세요.', finished: '이미 종료된 게임입니다.',
            error: `엔진 오류 (${state.engineStatus}). 기회는 유지됩니다.`
        };
        feedback(messages[state.status], !['correct', 'wrong'].includes(state.status));
        $('guessInput').value = '';
    } catch (error) {
        feedback(`요청 실패: ${error.message}. 서버 상태를 확인한 뒤 다시 시작해주세요.`, true);
        finished = true;
    } finally {
        lock(false);
        refreshRoom();
        if (!finished) $('guessInput').focus();
    }
});
$('restartButton').addEventListener('click', () => start());
$('resultRestartButton').addEventListener('click', () => start());
$('guessInput').setAttribute('aria-label', '정답 단어');
$('todayDate').textContent = new Intl.DateTimeFormat('ko-KR', {
    timeZone: 'Asia/Seoul', year: 'numeric', month: 'long', day: 'numeric'
}).format(new Date());
document.querySelector('.demo-badge').textContent = 'LOCAL PLAY';
document.querySelector('.notice').textContent = '같은 주소의 같은 탭에서 방과 기록을 복구합니다. 서버가 재시작되어도 저장된 게임은 유지되며, 마지막 활동 후 1시간이 지나면 만료됩니다.';
const roomPanel = document.createElement('section');
roomPanel.className = 'panel side-panel';
roomPanel.setAttribute('aria-label', '친구방');
roomPanel.innerHTML = `
    <div class="side-title">친구와 같은 단어 맞히기</div>
    <form id="nicknameForm" style="margin-top:18px">
        <label class="side-text" for="nicknameInput">내 닉네임 (선택, 최대 12자)</label>
        <input class="word-input" id="nicknameInput" placeholder="예: 말랑구름"
            maxlength="24" autocomplete="off" style="width:100%;margin-top:8px">
        <button class="restart" id="saveNameButton" type="submit">닉네임 저장</button>
        <p class="side-text" id="nicknameStatus" role="status"></p>
    </form>
    <p class="side-text" id="roomStatus" role="status">현재 혼자 플레이 중입니다.</p>
    <div id="roomPlayersPanel" hidden>
        <div class="side-title">친구방 현재 순위</div>
        <p class="side-text">정답을 맞힌 제출 횟수가 적을수록 높은 순위입니다. 같은 횟수는 공동 순위이며, 중복·미등록 단어는 세지 않습니다.</p>
        <ul id="roomPlayers" class="history-list"></ul>
        <p id="roomResultSummary" class="side-text"></p>
        <p id="roomSyncStatus" class="side-text" role="status"></p>
    </div>
    <button class="restart" id="createRoomButton" type="button">새 방 만들기</button>
    <button class="restart" id="recoverButton" type="button">현재 게임 다시 불러오기</button>
    <form id="joinRoomForm" style="margin-top:18px">
        <label class="side-text" for="roomCodeInput">친구의 방 코드</label>
        <input class="word-input" id="roomCodeInput" placeholder="영문·숫자 6자리"
            maxlength="6" autocomplete="off" autocapitalize="characters"
            style="width:100%;margin-top:8px;text-transform:uppercase">
        <button class="restart" id="joinRoomButton" type="submit">코드로 참가</button>
    </form>
    <p class="side-text">방 생성·참가 시 내 게임이 새로 시작됩니다. 나갈 때는 '방 나가기'를 눌러주세요. 탭만 닫으면 최대 1시간 동안 참가자로 남을 수 있습니다.</p>
`;
document.querySelector('.sidebar').prepend(roomPanel);
function validName(name) {
    if ([...name].length < 1 || [...name].length > 12) {
        $('nicknameStatus').textContent = '닉네임은 1~12자로 입력해주세요.';
        return false;
    }
    return true;
}
$('nicknameForm').addEventListener('submit', async event => {
    event.preventDefault();
    if (busy) return;
    const name = $('nicknameInput').value.trim();
    if (!validName(name)) return;
    lock(true);
    try {
        const result = await request('/api/profile', { name });
        $('nicknameStatus').textContent = `저장한 닉네임: ${result.name}`;
    } catch (error) {
        $('nicknameStatus').textContent = `저장 실패: ${error.message}`;
    } finally {
        lock(false);
        refreshRoom();
    }
});
$('createRoomButton').addEventListener('click', () => start('/api/rooms/create'));
$('joinRoomForm').addEventListener('submit', event => {
    event.preventDefault();
    const code = $('roomCodeInput').value.trim().toUpperCase();
    if (!/^[A-Z0-9]{6}$/.test(code)) {
        feedback('방 코드는 영문과 숫자 6자리입니다.', true);
        return;
    }
    start('/api/rooms/join', { code });
});
async function refreshRoom() {
    if (!roomCode || busy || roomPolling) return;
    const expectedRoom = roomCode;
    const expectedSession = sessionId;
    roomPolling = true;
    try {
        // Keep status requests separate from gameplay requests: an old status
        // response must not overwrite the current session after a room change.
        const response = await fetch('/api/rooms/status', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-Mallangi-Session': expectedSession },
            body: '{}',
            signal: AbortSignal.timeout(15000)
        });
        const state = await response.json();
        if (roomCode !== expectedRoom || sessionId !== expectedSession) return;
        if (!response.ok) throw new Error(state.error || '참가자 조회 실패');
        if (state.roomCode !== roomCode) return;
        $('roomPlayers').replaceChildren();
        for (const player of state.players) {
            const row = document.createElement('li');
            row.className = 'history-item';
            row.style.gridTemplateColumns = 'minmax(0, 1fr)';
            const name = document.createElement('span');
            name.className = 'history-word';
            name.textContent = `${player.rank ? player.rank + '위 · ' : ''}${player.name} · #${player.number}${player.isMe ? ' (나)' : ''}`;
            name.style.overflowWrap = 'anywhere';
            const status = document.createElement('span');
            status.className = 'history-score';
            status.textContent = player.won ? `${player.attemptsUsed}번 만에 성공 ✓`
                : player.finished ? '5번 제출 · 기회 소진'
                : `${player.attemptsUsed}번 제출 · 남은 ${player.remaining}회`;
            row.append(name, status);
            $('roomPlayers').append(row);
        }
        const successCount = state.players.filter(player => player.won).length;
        $('roomResultSummary').textContent = state.allFinished
            ? `현재 참가자 모두 종료 · ${successCount}명 성공`
            : `${successCount}명 성공 · 다른 참가자의 도전이 진행 중입니다.`;
        $('roomSyncStatus').textContent = `${state.players.length} / ${state.maxPlayers}명 · 3초마다 갱신`;
    } catch (error) {
        if (roomCode === expectedRoom && sessionId === expectedSession) {
            $('roomSyncStatus').textContent = `목록 갱신 실패: ${error.message}`;
        }
    } finally {
        roomPolling = false;
    }
}
function saveSession(token) {
    try {
        if (token) sessionStorage.setItem(SESSION_KEY, token);
        else sessionStorage.removeItem(SESSION_KEY);
    } catch {
        // Storage-disabled browsers can still play for the lifetime of the page.
    }
}

async function recover() {
    if (busy) return;
    if (!sessionId) return start();
    lock(true);
    let expired = false;
    try {
        const state = await request('/api/state', {});
        $('nicknameInput').value = state.name;
        $('guessInput').value = '';
        render(state);
        feedback('이전 게임을 복구했습니다. 카드·기회·기록을 확인해주세요.');
    } catch (error) {
        if (error.status === 409) {
            sessionId = '';
            saveSession('');
            expired = true;
        } else {
            feedback(`복구 실패: ${error.message}. 서버 연결을 확인한 뒤 다시 불러오세요.`, true);
        }
    } finally {
        lock(false);
    }
    if (expired) {
        await start();
        feedback('이전 게임이 만료되어 새 게임을 시작했습니다.', true);
    } else {
        refreshRoom();
    }
}

// A duplicated tab can inherit sessionStorage. Ask an existing page whether it
// owns that token before recovering, so normal tab duplication starts fresh.
let sessionChannel = null;
let checkingToken = '';
let occupied = false;
const pageId = Math.random().toString(36).slice(2);
if (typeof BroadcastChannel !== 'undefined') {
    sessionChannel = new BroadcastChannel('mallangi.tab-owner.v1');
    sessionChannel.onmessage = ({ data }) => {
        if (data.type === 'probe') {
            if (data.token === sessionId && sessionId) {
                sessionChannel.postMessage({ type: 'occupied', target: data.pageId, token: sessionId });
            } else if (data.token === checkingToken && checkingToken) {
                // If two copied tabs boot together, only one should recover.
                if (data.pageId < pageId) occupied = true;
                else sessionChannel.postMessage({ type: 'occupied', target: data.pageId, token: checkingToken });
            }
        } else if (data.type === 'occupied' && data.target === pageId && data.token === checkingToken) {
            occupied = true;
        }
    };
}

async function boot() {
    lock(true);
    let saved = '';
    try { saved = sessionStorage.getItem(SESSION_KEY) || ''; } catch {}
    if (saved && sessionChannel) {
        checkingToken = saved;
        sessionChannel.postMessage({ type: 'probe', token: saved, pageId });
        await new Promise(resolve => setTimeout(resolve, 300));
        checkingToken = '';
        if (occupied) saved = '';
    }
    sessionId = saved;
    if (!saved) saveSession('');
    lock(false);
    if (saved) await recover();
    else await start();
}
$('recoverButton').addEventListener('click', recover);
setInterval(refreshRoom, 3000);
boot();
