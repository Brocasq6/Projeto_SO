#!/bin/bash
# =============================================================================
# Gera tabela de resultados formatada a partir dos logs produzidos por run_tests.sh
# Uso: ./tests/generate_table.sh
# =============================================================================

RESULTS_DIR="./tests/results"
POLICIES=("FCFS" "RR")
PARALLEL_LEVELS=(1 2 4)

echo ""
echo "+-------------------------------+----------+----------+------------------+"
echo "| Política | Paralelismo        | N.º Cmds | Duração Média (s)| Total (ms)      |"
echo "+----------+--------------------+----------+------------------+-----------------+"

for POLICY in "${POLICIES[@]}"; do
    for PARALLEL in "${PARALLEL_LEVELS[@]}"; do
        LOG="$RESULTS_DIR/controller_${POLICY}_p${PARALLEL}.log"
        if [ -f "$LOG" ]; then
            AVG=$(awk -F'duration ' '{sum += $2; count++} END {if(count>0) printf "%.2f", sum/count; else print "N/A"}' "$LOG")
            COUNT=$(wc -l < "$LOG")
            # Extrair tempo total do summary
            TOTAL=$(grep "Política: $POLICY | Paralelismo: $PARALLEL" "$RESULTS_DIR/summary.txt" -A2 | grep "Tempo total" | awk '{print $NF}')
            printf "| %-8s | %-18s | %-8s | %-16s | %-15s |\n" \
                "$POLICY" "$PARALLEL" "$COUNT" "$AVG" "$TOTAL"
        else
            printf "| %-8s | %-18s | %-8s | %-16s | %-15s |\n" \
                "$POLICY" "$PARALLEL" "—" "sem dados" "—"
        fi
    done
done

echo "+----------+--------------------+----------+------------------+-----------------+"
echo ""
echo "Nota: 'Duração Média' é o tempo médio por comando registado no controller.log"
echo "      'Total' é o wallclock time de todo o workload (medido pelo script de teste)"
echo ""
