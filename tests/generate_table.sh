#!/bin/bash
# =============================================================================
# Gera tabelas ASCII a partir dos logs produzidos por run_tests.sh
# Uso: ./tests/generate_table.sh
# =============================================================================

RESULTS_DIR="./tests/results"
POLICIES=("FCFS" "RR")
PARALLEL_LEVELS=(1 2 4)
SCENARIOS=("FAIRNESS" "THROUGHPUT")

if [ ! -d "$RESULTS_DIR" ]; then
    echo "Erro: diretório $RESULTS_DIR não existe. Corre primeiro ./tests/run_tests.sh"
    exit 1
fi

print_table_for_scenario() {
    local SCENARIO=$1

    echo ""
    echo "=================================================================="
    echo "  Cenário: $SCENARIO"
    echo "=================================================================="
    echo "+--------+-----+----------+------------------+------------------+"
    echo "| Polít. | Par | N.º Cmds | Duração Méd. (s) | Turnaround Méd.  |"
    echo "+--------+-----+----------+------------------+------------------+"

    for POLICY in "${POLICIES[@]}"; do
        for PARALLEL in "${PARALLEL_LEVELS[@]}"; do
            local CTRL_LOG="$RESULTS_DIR/controller_${SCENARIO}_${POLICY}_p${PARALLEL}.log"
            local TIMING_LOG="$RESULTS_DIR/timing_${SCENARIO}_${POLICY}_p${PARALLEL}.log"

            local AVG_DUR="—"
            local COUNT="—"
            local AVG_TURN="—"

            if [ -f "$CTRL_LOG" ]; then
                AVG_DUR=$(awk -F'duration ' 'NF>1{split($2,a," "); sum+=a[1]; count++} END{if(count>0) printf "%.2f", sum/count; else print "—"}' "$CTRL_LOG")
                COUNT=$(grep -c '' "$CTRL_LOG")
            fi

            if [ -f "$TIMING_LOG" ]; then
                AVG_TURN=$(awk -F'=' '{gsub("ms",""); sum+=$2; n++} END{if(n>0) printf "%.0f ms", sum/n; else print "—"}' "$TIMING_LOG")
            fi

            printf "| %-6s | %-3s | %-8s | %-16s | %-16s |\n" \
                "$POLICY" "$PARALLEL" "$COUNT" "$AVG_DUR" "$AVG_TURN"
        done
    done

    echo "+--------+-----+----------+------------------+------------------+"
}

# Tabela específica do FAIRNESS — destaca a espera do user2
print_fairness_detail() {
    echo ""
    echo "=================================================================="
    echo "  Detalhe FAIRNESS — turnaround do user2 (vítima da monopolização)"
    echo "=================================================================="
    echo "+--------+-----+--------------------+"
    echo "| Polít. | Par | Espera User2       |"
    echo "+--------+-----+--------------------+"

    for POLICY in "${POLICIES[@]}"; do
        for PARALLEL in "${PARALLEL_LEVELS[@]}"; do
            local TIMING_LOG="$RESULTS_DIR/timing_FAIRNESS_${POLICY}_p${PARALLEL}.log"
            local U2="—"
            if [ -f "$TIMING_LOG" ]; then
                U2=$(awk -F'=' '/user2/{gsub("ms",""); print $2 " ms"; exit}' "$TIMING_LOG")
                [ -z "$U2" ] && U2="—"
            fi
            printf "| %-6s | %-3s | %-18s |\n" "$POLICY" "$PARALLEL" "$U2"
        done
    done

    echo "+--------+-----+--------------------+"
}

# -------------------------------------------------------------------
# Imprimir tabelas
# -------------------------------------------------------------------
for SCENARIO in "${SCENARIOS[@]}"; do
    print_table_for_scenario "$SCENARIO"
done

print_fairness_detail

echo ""
echo "Notas:"
echo "  - 'Duração Méd.' é a duração reportada no controller.log (tempo de execução real)."
echo "  - 'Turnaround Méd.' é o tempo desde a submissão do runner até ao seu fim (medido pelo script)."
echo "  - 'Espera User2' é o turnaround do único job do user2 — métrica de justiça."
echo ""