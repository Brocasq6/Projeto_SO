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
rm -f "$RESULTS_DIR"/*.log "$SUMMARY"

POLICIES=("FCFS" "RR")
PARALLEL_LEVELS=(1 2 4)

# -------------------------------------------------------------------
# Função: run_scenario <SCENARIO> <POLICY> <PARALLEL>
# -------------------------------------------------------------------
run_scenario() {
    local SCENARIO=$1
    local POLICY=$2
    local PARALLEL=$3
    local TAG="${SCENARIO}_${POLICY}_p${PARALLEL}"
    local TIMING_LOG="$RESULTS_DIR/timing_${TAG}.log"
    local RUNNER_LOG="$RESULTS_DIR/runner_${TAG}.log"
    local CTRL_LOG_COPY="$RESULTS_DIR/controller_${TAG}.log"

    rm -f controller.log /tmp/controller_fifo /tmp/runner_*_fifo "$TIMING_LOG" "$RUNNER_LOG"

    # Lançar o controller
    "$BIN_DIR/controller" "$PARALLEL" "$POLICY" &
    CTRL_PID=$!
    sleep 0.5  # garantir que o FIFO público está pronto

    local START
    START=$(date +%s%N)
    local RUNNER_PIDS=()

    if [ "$SCENARIO" = "FAIRNESS" ]; then
        # Cenário de Justiça:
        # User 1 submete 4 jobs de 2s cada → domina a fila em FCFS.
        # User 2 submete 1 job de 1s → fica "preso" atrás de tudo em FCFS,
        # mas é promovido rapidamente em RR.
        for i in 1 2 3 4; do
            {
                T_IN=$(date +%s%N)
                "$BIN_DIR/runner" -e 1 "sleep 2" >> "$RUNNER_LOG" 2>&1
                T_OUT=$(date +%s%N)
                echo "user1_job${i} turnaround=$(( (T_OUT - T_IN) / 1000000 ))ms" >> "$TIMING_LOG"
            } &
            RUNNER_PIDS+=($!)
            sleep 0.05  # espaçamento curto para garantir ordem de chegada
        done

        sleep 0.3  # garantir que os 4 jobs do user1 chegam primeiro ao controller

        {
            T_IN=$(date +%s%N)
            "$BIN_DIR/runner" -e 2 "sleep 1" >> "$RUNNER_LOG" 2>&1
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
                "$BIN_DIR/runner" -e "$UID_VAL" "$CMD" >> "$RUNNER_LOG" 2>&1
                T_OUT=$(date +%s%N)
                echo "user${UID_VAL} turnaround=$(( (T_OUT - T_IN) / 1000000 ))ms" >> "$TIMING_LOG"
            } &
            RUNNER_PIDS+=($!)
            sleep 0.05
        done
    fi

    # Esperar apenas pelos runners
    for PID in "${RUNNER_PIDS[@]}"; do
        wait "$PID" 2>/dev/null
    done

    local END
    END=$(date +%s%N)
    local TOTAL_MS=$(( (END - START) / 1000000 ))

    # Garantir que o controller já processou o último MSG_DONE antes de pedir shutdown
    sleep 0.3

    # Calcular métricas a partir do timing log
    local AVG_ALL="N/A"
    local U2_WAIT="N/A"
    if [ -f "$TIMING_LOG" ]; then
        AVG_ALL=$(awk -F'=' '{gsub("ms",""); sum+=$2; n++} END{if(n>0) printf "%.0f", sum/n; else print "N/A"}' "$TIMING_LOG")

        if [ "$SCENARIO" = "FAIRNESS" ]; then
            # No cenário FAIRNESS o user2 só tem 1 job; reportar o turnaround dele.
            U2_WAIT=$(awk -F'=' '/user2/{gsub("ms",""); print $2}' "$TIMING_LOG")
            [ -z "$U2_WAIT" ] && U2_WAIT="N/A"
        fi
    fi

    # Guardar cópia do controller.log com nome único por cenário/política/paralelismo
    if [ -f controller.log ]; then
        cp controller.log "$CTRL_LOG_COPY"
    fi

    # Output estruturado para o summary
    if [ "$SCENARIO" = "FAIRNESS" ]; then
        printf "  %-12s | %-6s | %-3s | %8s ms | %10s ms | %10s ms\n" \
            "$SCENARIO" "$POLICY" "$PARALLEL" "$TOTAL_MS" "$AVG_ALL" "$U2_WAIT" | tee -a "$SUMMARY"
    else
        printf "  %-12s | %-6s | %-3s | %8s ms | %10s ms | %10s\n" \
            "$SCENARIO" "$POLICY" "$PARALLEL" "$TOTAL_MS" "$AVG_ALL" "—" | tee -a "$SUMMARY"
    fi

    # Shutdown
    "$BIN_DIR/runner" -s >> /dev/null 2>&1
    wait "$CTRL_PID" 2>/dev/null
    sleep 0.2
}

# -------------------------------------------------------------------
# Cabeçalho
# -------------------------------------------------------------------
{
echo "=================================================================================="
echo "  RESULTADOS DOS TESTES DE ESCALONAMENTO — Projeto SO"
echo "  $(date)"
echo "=================================================================================="
echo ""
printf "  %-12s | %-6s | %-3s | %-11s | %-13s | %-13s\n" \
    "Cenário" "Polít." "Par" "Tempo Total" "Turnaround Md" "Espera User2"
echo "  -------------+--------+-----+-------------+---------------+---------------"
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
echo "=================================================================================="
echo "  Testes concluídos. Logs detalhados em: $RESULTS_DIR/"
echo "  - timing_*.log     : turnaround por job individual"
echo "  - runner_*.log     : output dos runners"
echo "  - controller_*.log : log do controller (durations registadas)"
echo "  Para a tabela formatada por cenário, corre: ./tests/generate_table.sh"
echo "=================================================================================="
} | tee -a "$SUMMARY"