# CSE 314 Offline 4 — সম্পূর্ণ Revision Document
### (তোমার আসল patch — 2205047.patch — এর উপর ভিত্তি করে লেখা)

---

## Part 1 — কেন এই Assignment (Problem ও Purpose)

**সমস্যা:** xv6-এর ডিফল্ট scheduler ছিল plain Round Robin — প্রতিটা process array-এর ক্রম অনুযায়ী পালাক্রমে সমান সময় পায়, কোনো priority নেই।

**সমস্যা এতে কী:**
- সব process-কে "সমান গুরুত্ব" দেওয়া বাস্তবসম্মত না — কিছু process-কে বেশি CPU share দেওয়া দরকার হতে পারে।
- Deterministic priority scheduling ব্যবহার করলে low-priority process **starve** করতে পারে (কখনো CPU-ই না পাওয়া)।

**সমাধান (এই assignment যা করতে বলেছে):**
1. **MLFQ (Multilevel Feedback Queue)** — ২টা queue বানানো (queue 0 = top, queue 1 = bottom), process তার আচরণ অনুযায়ী queue-র মধ্যে move করবে।
2. **Lottery Scheduling** (queue 0-তে) — ticket-ভিত্তিক probabilistic selection, proportional fairness দেওয়ার জন্য।
3. **Round Robin** (queue 1-তে) — সাধারণ পালাক্রমিক scheduling।
4. **Aging** — queue 1-এ অনেকক্ষণ আটকে থাকা process-কে জোর করে queue 0-তে তোলা, starvation ঠেকাতে।

---

## Part 2 — উঁচু স্তরের Flow (একদম বড় ছবি)

```
Timer Interrupt (hardware, প্রতি tick-এ)
      │
      ▼
kernel mode-এ ঢোকা (uservec/kernelvec)
      │
      ▼
usertrap() / kerneltrap()   [kernel/trap.c]
      │
      ├── update_time()          → এই CPU-তে যে চলছে, তার সময় গোনা + demotion চেক
      ├── age_waiting_procs()    → (শুধু cpuid()==0) সবার অপেক্ষার সময় গোনা + aging (BOOST)
      └── yield()
            │
            ▼
        sched() → swtch() → scheduler()-এ ফিরে যাওয়া
                                    │
                                    ▼
                        scheduler() [kernel/proc.c]
                        ├── Step 1: আগের process কি turn শেষ করেনি? → সরাসরি resume
                        ├── Step 2: fresh selection
                        │     ├── Queue 0: Lottery
                        │     └── Queue 1: Round Robin
                        └── Step 3: dispatch (swtch দিয়ে chosen-কে CPU দেওয়া)
```

---

## Part 3 — ফাইল অনুযায়ী কী কী করা হয়েছে (তোমার প্রকৃত কোড থেকে)

### 3.1 `kernel/param.h` — নতুন Macro

```c
#define TIME_LIMIT_0 2      // queue 0-এর time slice (ticks)
#define TIME_LIMIT_1 4      // queue 1-এর time slice (ticks)
#define WAIT_THRESH  6      // এতক্ষণ queue 1-এ অপেক্ষার পর aging হয়
#define DEFAULT_TICKETS 10  // প্রতিটা process যে সংখ্যক ticket দিয়ে শুরু করে
```
**কেন:** স্পেসিফিকেশনের ৪টা নির্দিষ্ট প্যারামিটার, যাতে পুরো কোড জুড়ে hardcoded সংখ্যা না থেকে একটাই জায়গায় বদলানো যায়।

---

### 3.2 `kernel/pstat.h` — নতুন ফাইল

```c
struct pstat {
  int pid[NPROC];
  int inuse[NPROC];
  int inQ[NPROC];
  int waiting_time[NPROC];
  int running_time[NPROC];
  int times_scheduled[NPROC];
  int tickets_original[NPROC];
  int tickets_current[NPROC];
  uint queue_ticks[NPROC][2];
};
```
**কেন:** `getpinfo()` syscall user-space-কে সব process-এর scheduling তথ্যের একটা snapshot দেয় — এই struct-টাই সেই snapshot-এর কাঠামো। Spec অনুযায়ী **হুবহু অপরিবর্তিত** রাখা হয়েছে।

---

### 3.3 `kernel/proc.h` — নতুন Field ও Extern

```c
extern struct spinlock sched_lock;   // struct cpu-এর পরে

struct proc {
  ... (আগের সব field)
  int in_queue;
  int tickets_original;
  int tickets_current;
  int ticks_in_turn;
  int waiting_ticks;
  int times_scheduled;
  uint queue_ticks[2];
};

extern struct proc proc[NPROC];      // struct proc সম্পূর্ণ define হওয়ার পরে
```
**কেন:**
- নতুন field-গুলো প্রতিটা process-এর scheduling state ট্র্যাক করার জন্য।
- `extern struct proc proc[NPROC];` অবশ্যই **struct proc-এর পূর্ণ সংজ্ঞার পরে** বসাতে হয়েছে — নইলে "incomplete type" compile error আসে (array বানাতে element-এর সাইজ জানা লাগে)।
- `sched_lock` একটা নতুন global spinlock, যেটা lottery/RR selection-কে multi-CPU-তে নিরাপদ করে।

---

### 3.4 `kernel/proc.c` — এখানেই মূল কাজ

**(ক) Global variable ও PRNG:**
```c
struct spinlock sched_lock;
int print_logs = 1;
static unsigned long rand_state = 2463534242UL;

int my_random(void) {
  rand_state ^= rand_state << 13;
  rand_state ^= rand_state >> 7;
  rand_state ^= rand_state << 17;
  return (int)(rand_state & 0x7fffffff);
}
```
**কেন:** xorshift-ভিত্তিক deterministic PRNG — fixed seed দিয়ে শুরু, তাই প্রতিবার একই sequence আসে (reproducibility, স্পেসিফিকেশনের দাবি অনুযায়ী), কিন্তু uniform distribution ভালো (lottery draw-এর জন্য জরুরি)।

**(খ) `procinit()`-এ নতুন লক init:**
```c
initlock(&sched_lock, "sched_lock");
```

**(গ) `allocproc()`-এ নতুন field init:**
```c
p->in_queue = 0;                     // নতুন process সবসময় queue 0-তে শুরু হয়
p->tickets_original = DEFAULT_TICKETS;
p->tickets_current = DEFAULT_TICKETS;
p->ticks_in_turn = 0;
p->waiting_ticks = 0;
p->times_scheduled = 0;
p->queue_ticks[0] = 0;
p->queue_ticks[1] = 0;
```
**কেন:** স্পেসিফিকেশনের rule 1 — "A newly created process always starts in queue 0"। এটাই একমাত্র জায়গা যেখানে **প্রতিটা** process (allocproc() দিয়েই সবাই আসে, সরাসরি হোক বা fork()-এর মধ্য দিয়ে) এই ডিফল্ট মান পায়।

**(ঘ) `freeproc()`-এ পরিষ্কার করা:**
```c
p->in_queue = 0;
p->tickets_original = 0;
p->tickets_current = 0;
... ইত্যাদি সব ০
```
**কেন:** একটা process শেষ হলে তার slot অন্য কারো জন্য পুনর্ব্যবহার হয় — পুরনো garbage value থেকে যাওয়া রোধ করার জন্য (ভালো defensive practice, যদিও `allocproc()` আবার init করে দেয়)।

**(ঙ) `fork()`-এ ticket inheritance:**
```c
np->tickets_original = p->tickets_current;
np->tickets_current  = p->tickets_current;
```
**কেন:** স্পেসিফিকেশন — "child inherits parent's CURRENT ticket count।" লক্ষ্য করো এটা parent-এর **current** (original না) মান নেয়।

**(চ) `sleep()`-এ voluntary promotion:**
```c
if (p->in_queue == 1) {
    if (print_logs)
        printf("PROMO: ...");
    p->in_queue = 0;
}
p->ticks_in_turn = 0;
```
**কেন:** rule 4 — "voluntarily gives up the CPU before its time slice is used up ... is promoted one level up।" `sleep()`-ই সেই একমাত্র জায়গা যেখান দিয়ে **সব ধরনের** voluntary blocking (I/O wait, pipe wait, wait() ইত্যাদি) যায় — তাই এখানেই বসানো হয়েছে।

**(ছ) `update_time()` — নতুন ফাংশন (প্রতি tick-এ, প্রতি CPU-তে):**
```c
void update_time(void) {
  struct proc *p = myproc();
  if(p == 0) return;
  acquire(&p->lock);
  if(p->state == RUNNING){
    p->ticks_in_turn++;
    p->queue_ticks[p->in_queue]++;
    p->waiting_ticks = 0;
    int limit = (p->in_queue==0) ? TIME_LIMIT_0 : TIME_LIMIT_1;
    if(p->ticks_in_turn >= limit){
      if(p->in_queue == 0){
        printf("DEMO: ...");
        p->in_queue = 1;
      }
      p->ticks_in_turn = 0;
    }
  }
  release(&p->lock);
}
```
**কেন:** শুধু **এই CPU-তে এখন যে চলছে** তার সময় গোনা — অন্য CPU-এর process স্পর্শ করে না, তাই multi-CPU-তেও নিরাপদ (একবারই গোনা হবে)। সময় ফুরালে demote করে এবং counter reset করে।

**(জ) `age_waiting_procs()` — নতুন ফাংশন (প্রতি global tick-এ একবার):**
```c
void age_waiting_procs(void) {
  for প্রতিটা p in proc[]:
    if(p->state == RUNNABLE){
      p->waiting_ticks++;
      p->queue_ticks[p->in_queue]++;
      if(p->in_queue==1 && p->waiting_ticks >= WAIT_THRESH){
        printf("BOOST: ...");
        p->in_queue = 0;
        p->waiting_ticks = 0;
        p->ticks_in_turn = 0;
      }
    }
}
```
**কেন:** পুরো array স্ক্যান করে, যারা RUNNABLE (অপেক্ষারত), তাদের সবার wait counter বাড়ায়। যে queue 1-এ WAIT_THRESH (৬) ticks পার করে ফেলেছে, তাকে জোর করে queue 0-তে তুলে দেয় (starvation প্রতিরোধ)।

**(ঝ) `scheduler()` — সম্পূর্ণ নতুন লজিক (৩টা ধাপে ভাগ করা):**

```
Step 1 (resume): আগের process কি তার turn শেষ করেনি?
    যদি হ্যাঁ → সরাসরি তাকেই আবার দাও, কোনো নতুন lottery/RR draw ছাড়াই

Step 2 (fresh selection, sched_lock দিয়ে সুরক্ষিত):
    ক) Queue 0 Lottery:
       - সব RUNNABLE+queue0 process-দের ticket যোগ করে total বের করা
       - my_random() % total দিয়ে winner index বাছাই
       - running sum দিয়ে ঠিক কোন process winner তা খুঁজে বের করা
       - winner->tickets_current--  
       - "Safe Reset Check": সবার ticket ০ হয়ে গেলে সবাইকে আবার original-এ reset
    খ) যদি Queue 0-তে কেউ RUNNABLE না থাকে → Queue 1 Round Robin
       - array-ক্রমে প্রথম RUNNABLE+queue1 process বেছে নেওয়া

Step 3 (dispatch):
    - fresh_pick হলে times_scheduled++
    - state = RUNNING, swtch() দিয়ে CPU দেওয়া
    - ফিরে আসার পর: turn এখনো শেষ না হলে resume = chosen (পরের loop-এ কাজে লাগবে)
```

**কেন এভাবে:** এটাই স্পেসিফিকেশনের সবচেয়ে জটিল অংশের সমাধান — "same process কে একাধিক tick ধরে রাখা, কিন্তু xv6 প্রতি tick-এ context switch করে বলে প্রতিবার নতুন lottery draw না করে সরাসরি আগেরজনকেই আবার resume করা।"

**(ঞ) `yield()`-এ null check:**
```c
if (p == 0) return;
```
**কেন:** বুটের প্রথম মুহূর্তে বা কিছু বিশেষ অবস্থায় `myproc()` NULL হতে পারে — এই check ছাড়া `acquire(&p->lock)` NULL pointer dereference করে crash করাতো (তোমার আগে দেখা সেই `panic: kerneltrap, stval=0` বাগটা এটাই ছিল)।

---

### 3.5 `kernel/trap.c` — timer tick হুক করা

`usertrap()` এবং `kerneltrap()` দুই জায়গাতেই:
```c
if (which_dev == 2) {
    update_time();
    if (cpuid() == 0)
        age_waiting_procs();
    yield();
}
```
**কেন `cpuid()==0` গার্ড:** `age_waiting_procs()` পুরো process array স্ক্যান করে — যদি প্রতিটা CPU নিজে নিজে এটা কল করতো, একই global tick-এ একটা process-এর `waiting_ticks` একাধিকবার বেড়ে যেতো (CPU সংখ্যার সমান গুণ)। শুধু CPU 0-কে দিয়ে এটা করালে, ঠিক একবারই হয় প্রতি tick-এ — এটাই **bonus (multi-CPU)** সঠিক করার মূল কৌশল।

---

### 3.6 System calls — `settickets` ও `getpinfo`

**`kernel/syscall.h`:**
```c
#define SYS_settickets 22
#define SYS_getpinfo   23
```

**`kernel/syscall.c`:** extern declaration + `syscalls[]` array-এ entry।

**`kernel/sysproc.c`:**
```c
uint64 sys_settickets(void) {
  int n;
  argint(0, &n);
  acquire(&p->lock);
  if (n < 1) {
    p->tickets_original = DEFAULT_TICKETS;
    p->tickets_current = DEFAULT_TICKETS;
    release(&p->lock);
    return -1;
  }
  p->tickets_original = n;
  p->tickets_current = n;
  release(&p->lock);
  return 0;
}

uint64 sys_getpinfo(void) {
  argaddr(0, &addr);
  if (addr == 0) return -1;
  // প্রতিটা non-UNUSED process-এর তথ্য ps struct-এ ভরা
  ...
  copyout(myproc()->pagetable, addr, (char*)&ps, sizeof(ps));
  return 0;
}
```
**লক্ষ্য করার বিষয়:** এই fork-এ `argint`/`argaddr` **`void`** রিটার্ন করে (mainline xv6-এর `int` রিটার্ন থেকে ভিন্ন) — তাই return value চেক না করে শুধু কল করা হয়েছে, error handling `addr==0` চেক আর পরে `copyout()`-এর return value দিয়ে হয়েছে।

**`user/user.h`, `user/usys.pl`:** normal syscall-এর মতোই prototype ও stub entry যোগ করা।

---

### 3.7 `user/dummyproc.c` ও `user/testprocinfo.c` — টেস্ট প্রোগ্রাম

**`dummyproc <tickets> <iterations>`:**
- `parse_u64()` দিয়ে বড় সংখ্যা parse করা (uint32 overflow এড়াতে)।
- `settickets()` কল করে ticket সেট করা।
- `fork()` — child periodically `sleep(2)` করে (PROMO trigger করার জন্য), parent শুধু CPU-bound লুপ চালায় (DEMO trigger করার জন্য)।

**`testprocinfo`:**
- `getpinfo()` কল করে পুরো snapshot নেয়।
- প্রতিটা non-zero PID-এর জন্য একটা লাইন প্রিন্ট করে (PID 0 বাদ দিয়ে)।

---

### 3.8 `Makefile`

```makefile
CPUS := 1          # (আগে ছিল 3)
UPROGS += $U/_dummyproc $U/_testprocinfo
```

### 3.9 `user/usertests.c` — সাইড ফিক্স
```c
void rwsbrk(char *s) { ... }   // আগে ছিল rwsbrk() — নতুন GCC-তে type mismatch error দিতো
```
এটা assignment-এর সাথে সম্পর্কহীন, শুধু নতুন compiler-এ compile করার জন্য দরকারি ফিক্স ছিল।

---

## Part 4 — একটা concrete উদাহরণ দিয়ে সম্পূর্ণ flow (ধাপে ধাপে)

ধরো, queue 0-তে process A (5 ticket) আছে, `TIME_LIMIT_0=2`।

1. **Tick ১:** A চলছে। Timer interrupt → `update_time()`: `ticks_in_turn` 0→1, limit(2) পার হয়নি। `yield()` → `scheduler()`-এ ফিরে যাওয়া।
2. `scheduler()`: `resume` চেক — A এখনো RUNNABLE, `ticks_in_turn(1) > 0` এবং `< limit(2)` → সরাসরি A-কেই আবার dispatch করা হয়, **নতুন lottery draw ছাড়াই**।
3. **Tick ২:** A আবার চলে। `update_time()`: `ticks_in_turn` 1→2, `2 >= 2` → **DEMO** — A কে queue 1-এ নামানো, `ticks_in_turn=0`।
4. `scheduler()`: `resume` চেক — `ticks_in_turn` এখন 0, শর্ত মিথ্যা → `resume` সেট হয় না। **Fresh selection** হয় — queue 0-তে যদি অন্য কেউ (B) থাকে, lottery-তে B জিততে পারে।

---

## Part 5 — Viva-তে সম্ভাব্য প্রশ্ন ও উত্তর

### সাধারণ কনসেপ্ট

**প্রশ্ন ১: MLFQ কী, এবং কেন এটা plain round robin-এর চেয়ে ভালো?**
> MLFQ হলো একাধিক queue-ভিত্তিক scheduling, যেখানে প্রতিটা queue-র আলাদা priority ও আলাদা scheduling policy থাকতে পারে। এটা ভালো কারণ এটা process-এর আচরণ (CPU-bound না I/O-bound) অনুযায়ী adaptively priority ঠিক করে — I/O-bound process দ্রুত response পায় (queue 0-তে promote হয়), আর CPU-bound process নিচের queue-তে চলে যায় যাতে সে ছোট, interactive process-দের ব্লক না করে।

**প্রশ্ন ২: Lottery scheduling কীভাবে fairness নিশ্চিত করে, deterministic না হয়েও?**
> প্রতিটা process-এর জেতার সম্ভাবনা তার ticket সংখ্যার সমানুপাতিক ($Pr[p_i] = t_i/\sum t_j$)। কোনো নির্দিষ্ট মুহূর্তে কে জিতবে সেটা random, কিন্তু বহুবার draw করলে long-run average CPU allocation ticket ratio-র কাছাকাছি চলে আসে (Law of Large Numbers)।

**প্রশ্ন ৩: Aging আর priority boosting-এর পার্থক্য কী?**
> Aging individually প্রতিটা process-এর wait time দেখে, শুধু threshold পার হওয়া process-কেই promote করে। Priority boosting periodically **সবাইকে** একসাথে top queue-তে তুলে দেয়, wait time বিবেচনা না করেই। এই assignment aging চায়, boosting না।

**প্রশ্ন ৪: তুমি কেন `sleep()`-এ promotion logic বসিয়েছো, অন্য কোথাও কেন না?**
> কারণ xv6-এর সব ধরনের voluntary blocking (I/O wait, pipe read/write wait, `wait()` syscall) শেষ পর্যন্ত `sleep()` ফাংশনের ভেতর দিয়েই যায় — এটাই একমাত্র কেন্দ্রীয় জায়গা যেখানে বসালে সব case কভার হয়।

**প্রশ্ন ৫: `update_time()` আর `age_waiting_procs()` — দুটো আলাদা ফাংশন কেন করলে, একটাতেই করা যেতো না?**
> `update_time()` শুধু এই CPU-তে RUNNING প্রসেস দেখে (per-CPU safe), আর `age_waiting_procs()` পুরো array স্ক্যান করে RUNNABLE process-দের aging করে (এটা গ্লোবাল, একবারই হওয়া উচিত)। এই বিভাজনই multi-CPU-তে সঠিকভাবে কাজ করার মূল কারণ — যদি একই ফাংশনে মিশিয়ে ফেলতাম আর প্রতিটা CPU নিজে নিজে পুরো array স্ক্যান করতো, তাহলে aging কয়েকগুণ দ্রুত হয়ে যেতো।

### Scheduler internals

**প্রশ্ন ৬: xv6 প্রতি timer tick-এ context switch করে, তাহলে কীভাবে একটা process ২ ticks ধরে queue 0-তে "থাকে"?**
> এটা `scheduler()`-এর `resume` variable দিয়ে হয়। প্রতি tick-এ process আসলেই CPU ছেড়ে scheduler()-এ ফিরে আসে, কিন্তু scheduler() চেক করে দেখে process-টার turn এখনো শেষ হয়নি (`ticks_in_turn < limit`), তখন সে নতুন lottery draw না করে সরাসরি সেই একই process-কে আবার দেয়। শুধু turn শেষ হলে (demote/promote হয়ে `ticks_in_turn=0` হলে) নতুন করে fresh selection হয়।

**প্রশ্ন ৭: `total_tickets` কীভাবে বের করা হয়, আর random winner কীভাবে বাছাই হয় (ফ্লোটিং পয়েন্ট ছাড়া)?**
> সব RUNNABLE+queue0 process-এর ticket যোগ করে total বের করা হয়। তারপর `my_random() % total` দিয়ে একটা random index (0 থেকে total-1) বাছাই করা হয়। এরপর প্রতিটা process-এর ticket যোগ করতে করতে (running sum) — যার যোগফল প্রথম সেই random index-কে ছাড়িয়ে যায়, সে-ই winner।

**প্রশ্ন ৮: PRNG deterministic কেন রাখা হয়েছে (fixed seed)?**
> Reproducibility-এর জন্য — একই সিডে বারবার একই sequence আসবে, তাই বাগ ধরা এবং কারো output verify করা সহজ হয়। xorshift ব্যবহার করা হয়েছে সাধারণ LCG-এর চেয়ে ভালো statistical distribution পাওয়ার জন্য।

**প্রশ্ন ৯: ticket সব process-এর ০ হয়ে গেলে কী হয়?**
> সব RUNNABLE queue-0 process-এর `tickets_current` যদি ০ হয়ে যায়, scheduler() সবাইকে আবার তাদের `tickets_original`-এ reset করে দেয় — এটাই স্পেসিফিকেশনের "ticket recycling" নিয়ম।

**প্রশ্ন ১০: `sched_lock` কেন দরকার?**
> Multi-CPU-তে দুটো CPU একই সময়ে scheduler() চালাতে পারে। `sched_lock` না থাকলে দুই CPU-ই একই সাথে lottery/RR selection করার চেষ্টা করলে একই process দুইবার বেছে নেওয়ার race condition হতে পারতো। এই lock নিশ্চিত করে selection ধাপটা একবারে একটা CPU-ই করে।

**প্রশ্ন ১১: `waiting_time` আর `running_time` (pstat-এর field) কী বোঝায়?**
> `waiting_time` মানে process বর্তমানে RUNNABLE অবস্থায় কতক্ষণ ধরে অপেক্ষা করছে (RUNNING হলেই ০-তে রিসেট হয়)। `running_time` মানে current turn-এ কতগুলো tick সে চালিয়েছে (queue পরিবর্তন হলে রিসেট হয়)। এই দুটোই cumulative না, বরং "বর্তমান দশা"-র measure।

**প্রশ্ন ১২: `queue_ticks[0]`/`queue_ticks[1]` কীভাবে আলাদা `waiting_time`/`running_time` থেকে?**
> `queue_ticks` হলো cumulative — process যতক্ষণ সেই queue-তে ছিল (RUNNING বা RUNNABLE, দুই অবস্থাতেই), তার total যোগফল, কখনো রিসেট হয় না। এটা lifetime-ব্যাপী পরিসংখ্যান, `waiting_time`/`running_time` এর মতো মুহূর্ত-ভিত্তিক না।

### System call ও data flow

**প্রশ্ন ১৩: `settickets(0)` বা negative দিলে কী হয়, কেন?**
> Return করে `-1`, আর ticket `DEFAULT_TICKETS`-এ (রি)সেট হয়ে যায় — স্পেসিফিকেশনের explicit নিয়ম, invalid input দিলে process সিস্টেমের ডিফল্ট শেয়ারে ফিরে যায়, crash বা undefined আচরণ না করে।

**প্রশ্ন ১৪: `fork()`-এ child কি parent-এর `tickets_original` না `tickets_current` পায়, আর কেন?**
> `tickets_current` পায়। কারণ স্পেসিফিকেশন স্পষ্ট বলেছে "child inherits parent's CURRENT ticket count"। যদি parent lottery-তে বহুবার জিতে তার current ticket কমে গিয়ে থাকে (ধরো ১০ থেকে ৭-এ নেমেছে), child সেই কমে যাওয়া মান (৭) নিয়েই শুরু করবে, আসল original (১০) দিয়ে না।

**প্রশ্ন ১৫: `getpinfo`-এ NULL pointer দিলে কী হয়?**
> `addr == 0` চেক আছে, সরাসরি `-1` রিটার্ন করে, কোনো `copyout()` চেষ্টাই করে না।

### Bonus / Multi-CPU

**প্রশ্ন ১৬: Multi-CPU সাপোর্ট করতে গিয়ে কোন কোন জায়গায় বিশেষ সাবধানতা নিতে হয়েছে?**
> ১) `resume` variable local (per-CPU) হওয়ায় প্রতিটা core নিজের turn আলাদাভাবে ট্র্যাক করে। ২) `sched_lock` দিয়ে lottery/RR selection সিরিয়ালাইজ করা হয়েছে। ৩) `update_time()` শুধু নিজের CPU-এর running process দেখে (double counting এড়াতে)। ৪) `age_waiting_procs()` শুধু `cpuid()==0`-এ কল হয়, যাতে প্রতি global tick-এ ঠিক একবারই aging হিসাব হয়।

**প্রশ্ন ১৭: `p->lock` আর `sched_lock` — দুটো lock কেন লাগে, একটাতেই হয় না কেন?**
> `p->lock` প্রতিটা individual process-এর state (তার নিজের field) সুরক্ষিত করে — এটা fine-grained, প্রতিটা process আলাদা লক নেয় বলে বেশি concurrency সম্ভব হয়। `sched_lock` হলো broader — "কে পরবর্তী চলবে" এই সিদ্ধান্তটাকে সুরক্ষিত করে, যেটা একাধিক process-এর তথ্য (total ticket যোগফল) একসাথে দেখার দরকার হয় বলে একটামাত্র লকে coordinate করতে হয়।

### Debug-related (তোমার নিজের অভিজ্ঞতা থেকে আসা প্রশ্ন হতে পারে)

**প্রশ্ন ১৮: তোমার implementation-এ `panic: kerneltrap` (stval=0) কেন হয়েছিল, কীভাবে ঠিক করলে?**
> বুটের প্রথম মুহূর্তে বা scheduler()-এর কিছু অবস্থায় `myproc()` NULL রিটার্ন করতে পারে (কোনো process CPU-তে running না)। `update_time()`/`yield()`-এ `myproc()`-এর ফলাফল সরাসরি dereference করলে (`p->lock`) NULL pointer dereference হয় (address 0 থেকে read), যেটা RISC-V-তে "load page fault" (scause 13, stval=0) হিসেবে ধরা পড়ে। সমাধান: `if(p == 0) return;` চেক যোগ করা `update_time()` আর `yield()` দুই জায়গাতেই।

**প্রশ্ন ১৯: `extern struct proc proc[NPROC];` কোথায় বসাতে হয়, কেন?**
> `struct proc`-এর সম্পূর্ণ সংজ্ঞার **পরে** — কারণ array বানাতে element-এর সম্পূর্ণ size জানা লাগে (incomplete type সমস্যা এড়াতে)। আগে বসালে "array type has incomplete element type" কম্পাইল error আসে।

---

## Part 6 — Sir-কে যেভাবে সংক্ষেপে বোঝাবে (এক মিনিটের সারাংশ)

> "আমি xv6-এর round-robin scheduler-কে ২-লেভেল MLFQ দিয়ে replace করেছি। Queue 0-তে lottery scheduling (ticket অনুপাতে random selection), queue 1-এ round robin। প্রতি timer tick-এ `update_time()` function চেক করে current process তার time slice শেষ করেছে কিনা — করলে demote করি। Voluntary blocking হলে (sleep() কলে) promote করি। আর প্রতি tick-এ `age_waiting_procs()` চেক করে queue 1-এ কেউ ৬ tick-এর বেশি অপেক্ষা করলে তাকে জোর করে queue 0-তে তুলে দিই, starvation এড়াতে। সবচেয়ে challenging অংশ ছিল বোঝা যে xv6 প্রতি tick-এ context switch করে বলে, একটা multi-tick time slice দিতে হলে scheduler()-কে মনে রাখতে হবে process এখনো তার turn শেষ করেনি — এজন্য আমি একটা `resume` pointer রেখেছি যেটা turn শেষ না হওয়া পর্যন্ত নতুন lottery draw না করেই আগের process-কেই আবার দেয়। এছাড়া bonus হিসেবে multi-CPU সাপোর্ট করেছি একটা `sched_lock` দিয়ে যাতে দুইটা CPU একই process না বেছে নেয়, আর aging-এর হিসাব যাতে CPU সংখ্যার সাথে ভুলভাবে স্কেল না করে সেজন্য শুধু `cpuid()==0`-এ সেটা করি।"
