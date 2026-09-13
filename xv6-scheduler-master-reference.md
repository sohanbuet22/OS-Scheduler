# xv6 Scheduler — Master Reference (সব Variant একসাথে)

> এক্সাম-এ দ্রুত খুঁজে পাওয়ার জন্য প্রতিটা variant আলাদা section-এ, নিজে নিজে সম্পূর্ণ (self-contained)। প্রথমে "Common Steps" পড়ে নাও — এগুলো সব variant-এই লাগবে।

---

# 🔧 Common Steps (সব Variant-এই লাগবে)

### Syscall যোগ করার Universal Pattern
যেকোনো নতুন syscall (`setpriority`, `setjoblen`, `getpriority` ইত্যাদি) যোগ করতে ৫ জায়গায় হাত দিতে হবে:

| File | কী করতে হবে |
|---|---|
| `kernel/syscall.h` | `#define SYS_xxx <next_number>` |
| `kernel/syscall.c` | `extern uint64 sys_xxx(void);` + array-তে `[SYS_xxx] sys_xxx,` |
| `kernel/sysproc.c` | আসল ফাংশন লজিক লেখা |
| `user/user.h` | prototype (argument count/type সহ ঠিক মিলিয়ে!) |
| `user/usys.pl` | `entry("xxx");` |

**⚠️ সবচেয়ে বেশি হওয়া ভুল:** `user.h`-এ prototype ভুল লেখা (যেমন argument নেয় এমন ফাংশনকে `(void)` লেখা) — এতে **compile-ই fail করবে**। সবসময় মিলিয়ে দেখো।

### `argint` ব্যবহার
Syscall-এর ভেতরে user থেকে পাঠানো argument নিতে:
```c
int n;
argint(0, &n);   // 0 = প্রথম argument
```

### Lock discipline (বারবার হওয়া bug-গুলো এড়াতে)
1. `struct proc`-এর যেকোনো field পড়া/লেখার আগে সেই process-এর `p->lock` **acquire** করো।
2. যতক্ষণ দরকার শুধু ততক্ষণই ধরে রাখো, কাজ শেষেই **release**।
3. **`swtch()`-এ সবসময় ঠিক pointer দাও** — যেটাকে select করেছো (`selected`/`p`), loop শেষের leftover pointer না।
4. যেই process-এর lock loop-এ ধরে রেখেছো (candidate হিসেবে), সেটা run করানোর পরে **অবশ্যই release করো** — ভুলে গেলে **deadlock**।
5. `if(selected==0 || condition)` লেখার সময় সাবধান — C-তে `&&` এর precedence `||`-এর চেয়ে বেশি, তাই `state==RUNNABLE` check বাদ পড়ে যেতে পারে। সবসময় বন্ধনী দিয়ে স্পষ্ট করো:
   ```c
   if(p->state == RUNNABLE && (selected == 0 || <compare>))
   ```

### Round Robin কীভাবে "এমনিতেই" হয়
কোনো `break` ছাড়া, `for(p=proc; p<&proc[NPROC]; p++)` loop-এ যত জনকে match করানো হয়, সবাইকে **একই pass-এ একে একে** চালালেই RR হয়ে যায় — কারণ প্রতি pass-এ সবাই ঠিক একবার করে সুযোগ পায়, আর পরের pass-ও একই ক্রমে আবার শুরু হয়। এর জন্য আলাদা কোনো queue/bookkeeping লাগে না।

### Makefile ও Submission (সব variant-এ একই)
```make
CPUS := 1
```
```bash
git add --all
git diff HEAD > ../<student_id>.patch
```

### `testloop.c`-এর common bug
- xv6 user-space `printf` সাধারণত `%u` সাপোর্ট করে না → সবসময় `int` + `%d` ব্যবহার করো, `uint32`/`%u` না।
- ফাইলের শেষে newline না থাকলে patch-এ `\ No newline at end of file` warning আসে — grading-এ সমস্যা না হলেও ভালো practice হলো newline রাখা।

---

# 1️⃣ Variant: Preemptive SJF / SRTF (Section A1)

### Problem
`testloop <iterations>` — এক argument (iteration count = job length, default 10)। **Preemptive SJF**: যেই process-এর remaining time সবচেয়ে কম, সে-ই চলবে। নতুন ছোট job এলে running process preempt হবে।

### Algorithm
1. প্রতিটা process-এর `joblen` (মোট length) আর `rutime` (এখন পর্যন্ত কত tick চলেছে) track করো।
2. `remaining = joblen - rutime`।
3. প্রতি timer tick-এ (scheduler স্বয়ংক্রিয়ভাবে re-invoke হয়), সব RUNNABLE-এর মধ্যে সবচেয়ে কম remaining-ওয়ালাকে বাছো।
4. Timer tick-এ running process-এর `rutime++` করতে হবে (নতুন কোড, `trap.c`-তে)।

### Solution

**`kernel/proc.h`:**
```c
struct proc {
  // ...existing...
  int joblen;     // মোট job length, default 10
  int rutime;     // কতগুলো tick চলেছে
};
```

**`kernel/proc.c` → `allocproc()`:**
```c
found:
  p->pid = allocpid();
  p->state = USED;
  p->joblen = 10;
  p->rutime = 0;
  // ...rest unchanged...
```

**`kernel/proc.c` → `scheduler()`:** (lock-held-candidate pattern — এটাই একমাত্র variant যেখানে এই pattern লাগে, কারণ single winner ক্রমাগত compare করতে হয়)
```c
void
scheduler(void)
{
  struct proc *p;
  struct proc *selected;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();
    selected = 0;

    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE &&
         (selected == 0 ||
          (p->joblen - p->rutime) < (selected->joblen - selected->rutime))){
        if(selected != 0)
          release(&selected->lock);
        selected = p;
        continue;              // p->lock ধরাই থাকলো (release skip)
      }
      release(&p->lock);
    }

    if(selected != 0){
      selected->state = RUNNING;
      c->proc = selected;
      swtch(&c->context, &selected->context);
      c->proc = 0;
      release(&selected->lock);
    }
  }
}
```

**`kernel/trap.c`** (`usertrap()` এবং `kerneltrap()`-এ timer-tick check-এর জায়গায়):
```c
if(which_dev == 2){
  if(myproc() != 0 && myproc()->state == RUNNING)
    myproc()->rutime++;
  yield();
}
```

**Syscall `setjoblen(int)`** (Common Steps অনুযায়ী ৫ জায়গায় যোগ করো):
```c
// sysproc.c
uint64
sys_setjoblen(void)
{
  int n;
  argint(0, &n);
  if(n <= 0) return -1;
  myproc()->joblen = n;
  return 0;
}
```

**`user/testloop.c`:**
```c
int n = atoi(argv[1]);
setjoblen(n);
// ...loop...
```

---

# 2️⃣ Variant: Priority-based Scheduler, No Aging (Section A2)

### Problem
`testloop <iterations> <priority>` — দুই argument। Default priority = **300**। **সংখ্যা বড় = priority বেশি।** Highest-priority process চলতে থাকবে যতক্ষণ না higher priority আসে (preempt)। সমান priority হলে RR।

### Algorithm
1. প্রতিটা process-এর `priority` (default 300)।
2. **Phase A:** সব RUNNABLE-এর মধ্যে max priority বের করো।
3. **Phase B:** যাদের priority == max, তাদের সবাইকে একই pass-এ একে একে চালাও (এতেই RR হয়ে যায়)।
4. এই two-phase approach-এর সুবিধা: কোনো lock-held-candidate জটিলতা লাগে না (Variant 1-এর মতো)।

### Solution

**`kernel/proc.h`:**
```c
struct proc {
  // ...existing...
  int priority;   // default 300, বেশি সংখ্যা = বেশি priority
};
```

**`kernel/proc.c` → `allocproc()`:**
```c
p->priority = 300;
```

**`kernel/proc.c` → `scheduler()`:**
```c
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();

    int maxprio = -1;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE && p->priority > maxprio)
        maxprio = p->priority;
      release(&p->lock);
    }

    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE && p->priority == maxprio){
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}
```
`trap.c`-তে কোনো পরিবর্তন লাগবে না (rutime দরকার নেই)।

**Syscall `setpriority(int)`** — Common Steps অনুযায়ী।

**`user/testloop.c`:**
```c
int n = atoi(argv[1]);
int prio = atoi(argv[2]);
setpriority(prio);
```

---

# 3️⃣ Variant: FCFS, শুধু pid 1/2 বাদে (Section B1)

### Problem
`testloop <iterations>` — এক argument। **pid 1 (init) ও pid 2 (sh)** default RR-এ চলবে (unaffected)। বাকি সব process **FCFS** (যে আগে আসছে, সে সম্পূর্ণ শেষ হওয়া পর্যন্ত চলবে, non-preemptive)।

### Algorithm
1. প্রতিটা process তৈরির সময় `arrival_time = ticks` (global tick counter) সংরক্ষণ করো।
2. **Phase A:** RUNNABLE-দের মধ্যে যাদের **pid > 2**, তাদের মধ্যে সবচেয়ে কম `arrival_time` (mini) বের করো — pid ≤ 2 এই প্রতিযোগিতায় অংশ নেবে না।
3. **Phase B:** run করাও তাদেরই যাদের (`pid <= 2`) **অথবা** (`arrival_time == mini`) — pid≤2 সবসময় সুযোগ পায় (RR অক্ষত থাকে, কারণ `||`-এর প্রথম অংশ সবসময় true), বাকিদের মধ্যে শুধু FCFS winner চলে।

### Solution

**`kernel/proc.h`:**
```c
struct proc {
  // ...existing...
  int arrival_time;
};
```

**`kernel/proc.c` → `allocproc()`:**
```c
p->arrival_time = ticks;   // proc.c-তে extern uint ticks; declare থাকা লাগবে যদি defs.h-এ না থাকে
```
(চাইলে strict করতে `acquire(&tickslock); p->arrival_time = ticks; release(&tickslock);` — optional, CPUS=1-এ প্রয়োজন কম।)

**`kernel/proc.c` → `scheduler()`:**
```c
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();

    int mini = 2000000000;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE && p->pid > 2 && p->arrival_time < mini)
        mini = p->arrival_time;
      release(&p->lock);
    }

    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE &&
         (p->pid <= 2 || p->arrival_time == mini)){
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}
```

**⚠️ এই variant-এ কোনো নতুন syscall লাগে না** — `arrival_time` kernel নিজেই `allocproc()`-এ বসিয়ে দেয়, user program-এর কিছু জানানোর দরকার নেই। `testloop.c` শুধু iteration count নিয়ে loop চালাবে, স্বাভাবিক।

### Optional Optimization (grading-এ প্রভাব ফেলে না, শুধু efficiency)
যে process এখন running, তাকে পরের round-এ আবার scan না করে সরাসরি চালানো যায় `static struct proc *current` রেখে — কিন্তু **এটা pid≤2-দের জন্য কখনো ব্যবহার কোরো না**, নাহলে তাদের RR ভেঙে যাবে।

---

# 4️⃣ Variant: Priority Scheduler + Aging (Section B2)

### Problem
`testloop <iterations> <priority>` — দুই argument। Default priority = **1000**। Highest priority চলবে, সমান priority-তে RR। **Aging:** কোনো process ৩০ tick ধরে unscheduled থাকলে তার priority +10 বাড়বে, আর তার waiting time reset হবে। Waiting time **schedule হলেও** reset হবে। Priority বাড়লে kernel থেকে print করতে হবে। `getpriority()`ও লাগবে।

### Algorithm
1. প্রতিটা process-এর `priority` (default 1000) আর `waiting_time` (default 0)।
2. **Phase A** (single loop, RUNNABLE-দের জন্য):
   - `waiting_time++`
   - যদি `waiting_time > 30`: `priority += 10`, `waiting_time = 0`, **kernel print করো**
   - তারপর `maxpriority` compare/update করো (aging-এর **পরে**, যাতে এই round-এই নতুন priority বিবেচিত হয়)
3. **Phase B:** যাদের priority == maxpriority, তাদের চালাও — **schedule হওয়ার মুহূর্তেই `waiting_time = 0` reset করতে ভুলো না** (এটাই সবচেয়ে common bug)।

### Solution

**`kernel/proc.h`:**
```c
struct proc {
  // ...existing...
  int priority;       // default 1000
  int waiting_time;   // default 0
};
```

**`kernel/proc.c` → `allocproc()`:**
```c
p->priority = 1000;
p->waiting_time = 0;
```

**`kernel/proc.c` → `scheduler()`:**
```c
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();

    int maxpriority = -1;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE){
        p->waiting_time++;
        if(p->waiting_time > 30){
          p->priority += 10;
          p->waiting_time = 0;
          printf("Priority of process %d increased to %d\n", p->pid, p->priority);
        }
        if(p->priority > maxpriority)
          maxpriority = p->priority;
      }
      release(&p->lock);
    }

    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE && p->priority == maxpriority){
        p->waiting_time = 0;     // ⚠️ schedule হলে reset — সবচেয়ে বেশি ভুলে যাওয়া লাইন
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}
```

**Syscalls (দুইটা):**
```c
// sysproc.c
uint64
sys_setpriority(void)
{
  int n;
  argint(0, &n);
  if(n < 0) return -1;
  myproc()->priority = n;
  return 0;
}

uint64
sys_getpriority(void)
{
  int n;
  struct proc *p = myproc();
  acquire(&p->lock);
  n = p->priority;
  release(&p->lock);
  return n;
}
```
(দুটোই Common Steps-এর ৫-জায়গা pattern অনুযায়ী register করতে হবে, `user.h`-এ দুটোই `int`।)

**`user/testloop.c`:**
```c
int n = atoi(argv[1]);
int prio = atoi(argv[2]);
setpriority(prio);
```

---

# 📋 Quick Comparison Table

| | A1: SJF/SRTF | A2: Priority (no aging) | B1: FCFS (pid≤2 exception) | B2: Priority + Aging |
|---|---|---|---|---|
| testloop args | 1 (iterations) | 2 (iterations, priority) | 1 (iterations) | 2 (iterations, priority) |
| Default field value | joblen=10 | priority=300 | arrival_time=ticks | priority=1000, waiting_time=0 |
| Extra field | `joblen`, `rutime` | `priority` | `arrival_time` | `priority`, `waiting_time` |
| New syscall | `setjoblen` | `setpriority` | কোনটাই না | `setpriority`, `getpriority` |
| `trap.c` change | হ্যাঁ (`rutime++`) | না | না | না |
| Scheduler pattern | lock-held-candidate (single winner compare) | two-phase (max then filter) | two-phase (min then filter) | two-phase (aging + max, then filter+reset) |
| Special exception | নেই | নেই | pid ≤ 2 → RR bypass | নেই |
| Extra kernel print | না | না | না | হ্যাঁ (priority বাড়লে) |

---

# 🎯 Exam-এ Checklist (যেকোনো variant শুরু করার আগে)

1. Problem statement থেকে বের করো: **কোন metric দিয়ে ranking হবে** (job length? priority? arrival time?) এবং **সংখ্যা ছোট না বড় জেতে** সেটা confirm করো (Note-এর example থেকে verify করো, assume কোরো না)।
2. `struct proc`-এ কোন field(s) লাগবে ঠিক করো।
3. `allocproc()`-এ default value বসাও।
4. দরকার হলে syscall বানাও (Common Steps pattern)।
5. `scheduler()`-এ two-phase pattern দিয়ে শুরু করো (max/min বের করা, তারপর filter করে চালানো) — এটাই বেশিরভাগ ক্ষেত্রে safest, lock-held-candidate pattern শুধু তখনই লাগবে যখন single winner-কে continuous compare করতে হয় remaining-time-এর মতো কিছুর জন্য।
6. Reset/print-এর মতো "note"-এ উল্লেখিত extra requirement আছে কিনা আবার পড়ে দেখো (এগুলোই সবচেয়ে বেশি miss হয়)।
7. `Makefile`-এ `CPUS := 1` ভুলো না।
8. Submit করার আগে compile করে actually চালিয়ে output মিলিয়ে দেখো।
