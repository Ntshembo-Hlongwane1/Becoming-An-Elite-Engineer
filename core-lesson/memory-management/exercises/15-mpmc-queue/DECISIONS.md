# Decisions (one line each: decision — why — source)

## cell seq load = acquire / store = release — the per-cell happens-before for data (15.4 §2 / 14.3)

## pos CASes stay relaxed — they order nothing cross-thread beyond the seq handoff (15.4 §2)

## per-cell sequence numbers — how they give MPMC correctness AND ABA-freedom (15.3 §4 / 15.4 §2)

## bounded array / no node allocation — why this removes the reclamation problem (15.2 / 15.4 §2)

## dif comparison: ==0 ready, <0 full/empty, >0 another thread advanced -> retry (15.4 §2)

## alignas(64) on enqueue_pos_/dequeue_pos_ — producer/consumer false sharing (Lesson 6.4)

## why TSan cannot prove ABA/reclamation safety, only data-race freedom (15.3 §3 / 15.5)
