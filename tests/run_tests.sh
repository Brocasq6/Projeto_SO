#!/bin/bash
# =============================================================================
# Testes de Avaliação de Políticas de Escalonamento — Projeto SO
# Uso: ./tests/run_tests.sh
#
# Dois cenários de teste:
#   FAIRNESS   — user 1 monopoliza a fila; mede quão justo é cada política
#   THROUGHPUT — carga mista de 3 utilizadores; mede desempenho global
# =============================================================================

BIN_DIR="./bin"
RESULTS_DIR="./tests/results"
SUMMARY="$RESULTS_DIR/summary.txt"

mkdir -p "$RESULTS_DIR"

POLICIES=("FCFS" "RR")
PARALLEL_LEVELS=(1 2 4)

# -------------------------------------------------------------------
# Função: run_scenario <SCENARIO> <POLICY> <PARALLEL>
# -------------------------------------------------------------------
run_scenario() {
    local SCENARIO=$1
    local POLICY=$2
    local PARALLEL=$3
    local TIMING_LOG="$RESULTS_DIR/timing_${SCENARIO}_${POLICY}_p${PARALLEL}.log"

    rm -f controller.log /tmp/controller_fifo /tmp/runner_*_fifo "$TIMING_LOG"

    # Lançar o controller
    "$BIN_DIR/controller" "$PARALLEL" "$POLICY" &
    CTRL_PID=$!
    sleep 0.3

    local START
    START=$(date +%s%N)
    local RUNNER_PIDS=()

    if [ "$SCENARIO" = "FAIRNESS" ]; then
        # Cenário de Justiça:
        # User 1 submete 4 jobs de 2s cada → domina a fila em FCFS
        # User 2 submete 1 job de 1s → fica "preso" atrás de tudo em FCFS
        #                               mas é promovido rapidamente em RR
        for i in 1 2 3 4; do
            {
                T_IN=$(date +%s%N)
                "$BIN_DIR/runner" -e 1 "sleep 2" >> "$RESULTS_DIR/runner_${SCENARIO}_${POLICY}_p${PARALLEL}.log" 2>&1
                T_OUT=$(date +%s%N)
                echo "user1_job${i} turnaround=$(( (T_OUT - T_IN) / 1000000 ))ms" >> "$TIMING_LOG"
            } &
            RUNNER_PIDS+=($!)
        done

        sleep 0.25  # garantir que os 4 jobs do user1 chegam primeiro ao controller

        {
            T_IN=$(date +%s%N)
            "$BIN_DIR/runner" -e 2 "sleep 1" >> "$RESULTS_DIR/runner_${SCENARIO}_${POLICY}_p${PARALLEL}.log" 2>&1
            T_OUT=$(date +%s%N)
            echo "user2_job1 turnaround=$(( (T_OUT - T_IN) / 1000000 ))ms" >> "$TIMING_LOG"
        } &
        RUNNER_PIDS+=($!)

    elif [ "$SCENARIO" = "THROUGHPUT" ]; then
        # Cenário de Throughput:
        # 3 utilizadores com cargas mistas — mede desempenho global
        local WORKLOAD=(
            "1:sleep 3"
            "2:sleep 1"
            "3:sleep 2"
            "1:sleep 1"
            "2:sleep 2"
            "3:sleep 1"
        )
        for ENTRY in "${WORKLOAD[@]}"; do
            UID_VAL="${ENTRY%%:*}"
            CMD="${ENTRY##*:}"
            {
                T_IN=$(date +%s%N)
                "$BIN_DIR/runner" -e "$UID_VAL" "$CMD" >> "$RESULTS_DIR/runner_${SCENARIO}_${POLICY}_p${PARALLEL}.log" 2>&1
                T_OUT=$(date +%s%N)
                echo "user${UID_VAL} turnaround=$(( (T_OUT - T_IN) / 1000000 ))ms" >> "$TIMING_LOG"
            } &
            RUNNER_PIDS+=($!)
        done
    fi

    # Esperar apenas pelos runners
    for PID in "${RUNNER_PIDS[@]}"; do
        wait "$PID" 2>/dev/null
    done

    local END
    END=$(date +%s%N)
    local TOTAL_MS=$(( (END - START) / 1000000 ))

    # Calcular turnaround médio e máximo do user 2 (cenário FAIRNESS)
    local AVG_ALL MAX_U2 AVG_U2
    if [ -f "$TIMING_LOG" ]; then
        AVG_ALL=$(awk -F'=' '{gsub("ms",""); sum+=$2; n++} END{printf "%.0f", sum/n}' "$TIMING_LOG")
        MAX_U2=$(grep "user2" "$TIMING_LOG" | awk -F'=' '{gsub("ms",""); if($2>max) max=$2} END{printf "%.0f", max}' "$TIMING_LOG")
        AVG_U2=$(grep "user2" "$TIMING_LOG" | awk -F'=' '{gsub("ms",""); sum+=$2; n++} END{if(n>0) printf "%.0f", sum/n; else print "N/A"}' "$TIMING_LOG")
    fi

    # Guardar métricas
    if [ -f controller.log ]; then
        cp controller.log "$RESULTS_DIR/controller_${SCENARIO}_${POLICY}_p${PARALLEL}.log"
    fi

    # Output estruturado para o summary
    printf "  %-12s | %-6s | %-5s | Total: %5s ms | Turnaround médio: %s ms | Espera user2: %s ms\n" \
        "$SCENARIO" "$POLICY" "$PARALLEL" "$TOTAL_MS" "$AVG_ALL" "$AVG_U2" | tee -a "$SUMMARY"

    # Shutdown
    "$BIN_DIR/runner" -s >> /dev/null 2>&1
    wait "$CTRL_PID" 2>/dev/null
    sleep 0.2
}

# -------------------------------------------------------------------
# Cabeçalho
# -------------------------------------------------------------------
{
echo "=============================================================="
echo "  RESULTADOS DOS TESTES DE ESCALONAMENTO — Projeto SO"
echo "  $(date)"
echo "=============================================================="
echo ""
printf "  %-12s | %-6s | %-5s | %-18s | %-26s | %-20s\n" \
    "Cenário" "Polít." "Par." "Tempo Total" "Turnaround Médio" "Espera User2"
echo "  -------------|--------|-------|--------------------|-----------------------------|---------------------"
} | tee "$SUMMARY"

# -------------------------------------------------------------------
# Executar todos os cenários
# -------------------------------------------------------------------
for SCENARIO in "FAIRNESS" "THROUGHPUT"; do
    for POLICY in "${POLICIES[@]}"; do
        for PARALLEL in "${PARALLEL_LEVELS[@]}"; do
            run_scenario "$SCENARIO" "$POLICY" "$PARALLEL"
        done
    done
done

{
echo ""
echo "=============================================================="
echo "  Testes concluídos. Logs detalhados em: $RESULTS_DIR/"
echo "  Ficheiros timing_*.log têm turnaround por job individual."
echo "=============================================================="
} | tee -a "$SUMMARY"
