# Roadmap

Cada etapa termina com um critério de aceite verificável. Só depois disso ela vira código Ansible e ganha uma release.

| Etapa | Entrega | VM | Critério de aceite | Release |
| --- | --- | --- | --- | --- |
| 1. RAN + core + E2 | gNB e UE em rfsim ligados ao CN5G; FlexRIC recebe o E2 Setup; xApp de KPM rodando | `ran` | `ping` do UE até o ext-dn funciona; `xapp_kpm_moni` mostra métricas por UE | `v0.1.0` |
| 2. Multi-slice | Dois slices (S-NSSAI) no core e no gNB, um UE em cada; xApp que altera a partição de recursos | `ran` | Mudar a cota de um slice muda a vazão medida com `iperf3` | `v0.2.0` |
| 3. Caminho O-RAN com FlexRIC | SMO + Non-RT RIC da O-RAN SC, rApp de SLA com API de intenção, terminação A1-P própria no FlexRIC, xApp de slicing controlada por A1, O1 Adapter do OAI + OAM da O-RAN SC | `smo` + `ran` | Uma intenção enviada ao rApp vira política A1 → ação E2, e a mudança de PRBs e vazão do slice aparece no KPM e no `iperf3`; o A1 PMS da O-RAN SC opera a terminação A1-P; o gNB aparece no SMO via O1 | `v0.3.0` |
| 4. Interface Y1 | Produtor Y1 (REST + mTLS) sobre os dados das xApps; consumidor Y1 | `ran` + `obs` | Consumidor assina RAI e recebe notificações periódicas do próprio slice | `v0.4.0` |
| 5. Observabilidade | OpenTelemetry Collector, Prometheus, Grafana e traces das chamadas Y1 | `obs` | Dashboard mostra vazão e PRBs por slice em quase tempo real | `v0.5.0` |
| 6. Botão de ação e atalho | Botão no portal que chama a API de intenção do rApp; atalho: serviço de controle que fala direto com a xApp de slicing, para a comparação do artigo 1 | `obs` + `smo` + `ran` | O botão altera a cota pelos dois caminhos (atalho e O-RAN); o efeito aparece no dashboard e reverte no prazo | `v0.6.0` |
| 7. Automação e experimento | Todas as roles, serviços systemd e script do experimento | todas | `vagrant up` sobe tudo do zero e o experimento roda sem passo manual | `v1.0.0` |
| 8. Near-RT RIC da O-RAN SC | Troca do FlexRIC pelo Near-RT RIC da O-RAN SC (A1 Mediator nativo), xApp de slicing portada; todo o resto igual | `ran` | O mesmo experimento roda com os dois Near-RT RICs e gera a comparação | `v1.1.0` |

A ordem foi revista em 2026-11-26 ([decisão 24](decisions.md#24-caminho-o-ran-antes-da-exposição-de-dados)): o caminho O-RAN completo (antes Etapa 6) vem logo depois da Etapa 2, para o cenário de ponta a ponta ser validado antes das camadas de exposição (Y1, observabilidade e portal). Na Etapa 3, a entrada do laço é a API de intenção do rApp, chamada por linha de comando; na Etapa 6, o botão do portal passa a chamar a mesma API.

## Componentes prontos e componentes próprios

O projeto usa software pronto sempre que existe e escreve código próprio só onde não há implementação open source.

| Função | Componente | Origem |
| --- | --- | --- |
| Non-RT RIC, A1 Policy Management Service, painel de políticas | O-RAN SC NONRTRIC | Pronto |
| A1 Simulator (testes da terminação A1-P) | O-RAN SC `sim/a1-interface` | Pronto |
| SMO para O1: cliente NETCONF (SDN-R/OpenDaylight) e VES Collector | O-RAN SC OAM | Pronto |
| Servidor O1 do gNB (NETCONF/YANG + alarmes) | O1 Adapter do OAI | Pronto |
| Terminação A1-P do lado Near-RT RIC para o FlexRIC (Etapa 3) | Serviço REST seguindo a OpenAPI do A1AP | Próprio |
| A1 do lado Near-RT RIC na Etapa 8 | A1 Mediator do Near-RT RIC da O-RAN SC | Pronto |
| Produtor e consumidor Y1 | Serviços REST seguindo o Y1AP | Próprio |
| rApp de SLA, xApp de slicing, serviço de controle, portal | — | Próprio |

## Publicações planejadas

| Artigo | Base | Pergunta | Contribuições esperadas |
| --- | --- | --- | --- |
| 1 | Etapas 1 a 7 | É possível fechar o laço intenção → A1 → E2, com exposição Y1, usando componentes open source e o mínimo de código próprio? | Terminação A1-P aberta para o FlexRIC, validada contra o A1 PMS da O-RAN SC; implementação de Y1; medição do laço de ponta a ponta; laboratório reproduzível |
| 2 | Etapa 8 | O que muda, no mesmo caso de uso, entre um Near-RT RIC leve com terminação A1 própria (FlexRIC) e um com A1 nativo (O-RAN SC)? | Comparação controlada de desempenho do laço, recursos, conformidade, esforço de desenvolvimento e portabilidade da xApp |

A novidade dos dois artigos depende de uma revisão de literatura antes de fechar o escopo.

## Experimento final

**Hipótese:** um consumidor do slice B, ao pedir mais banda no dashboard, obtém aumento de vazão sem violar o mínimo garantido do slice A, e a rede volta ao estado anterior ao fim do prazo.

**Cenário:** UE1 no slice A e UE2 no slice B, os dois com downlink saturado por `iperf3`; partição inicial 50/50; mínimo garantido do slice A de 30%.

**Roteiro de uma rodada:**

1. Linha de base: 2 minutos sem intervenção.
2. O consumidor B aperta "+banda por 5 min" no dashboard.
3. O pedido percorre o atalho (Etapa 6) ou o caminho O-RAN (Etapas 3 e 8).
4. A vazão de B sobe, a de A cai e para no mínimo garantido.
5. Teste negativo: o consumidor B tenta agir sobre o slice A e o pedido é recusado.
6. Ao fim dos 5 minutos, a partição volta a 50/50.
7. Coleta de métricas, traces e logs do `iperf3`.

**Métricas:**

| Métrica | Fonte |
| --- | --- |
| Vazão DL por slice (Mbps) | `iperf3` e KPM via Y1 |
| Uso de PRB por slice (%) | KPM via Y1 |
| Tempo do clique até o comando E2 (ms) | Traces do OpenTelemetry |
| Tempo do clique até a vazão estabilizar (s) | Prometheus |
| Violações do mínimo do slice A | Prometheus |
| Pedidos recusados corretamente | Auditoria do serviço de controle ou do rApp |
| CPU e RAM do Near-RT RIC, em repouso e sob carga | Prometheus (node/cAdvisor) |

**Comparações:**

- **Artigo 1:** 10 rodadas pelo atalho e 10 pelo caminho O-RAN, comparando média e desvio dos tempos de reação.
- **Artigo 2:** 10 rodadas pelo caminho O-RAN com cada Near-RT RIC (FlexRIC e O-RAN SC), com o mesmo gNB, core, rApp, consumidor Y1, dashboard e roteiro.

Para a comparação ser limpa, as métricas, os nomes das séries no Prometheus e os scripts do experimento são definidos na Etapa 7 já pensando na Etapa 8.

## Riscos e decisões em aberto

| Risco ou decisão | Impacto | Como tratar |
| --- | --- | --- |
| Controle de slice no OAI `2026.w46` (SLICE SM do FlexRIC, E2SM-RC ou E2SM-CCC) | Define a xApp das Etapas 3 e 6, e o quanto dela é portável para a Etapa 8 | A [investigação](stages/02-multi-slice.md#investigação-controle-de-slice) mostrou que nenhum dos três chega ao escalonador MAC, que não tem escalonamento por slice. Decidido: escalonador por slice num fork próprio do OAI, primeiro com cotas fixas no `.conf` do gNB e depois com E2SM-RC estilo 2 (decisão 20) |
| FlexRIC não termina A1 | A Etapa 3 exige uma peça própria | Terminação A1-P seguindo a OpenAPI do A1AP, testada contra o A1 Simulator e o A1 PMS da O-RAN SC |
| Near-RT RIC da O-RAN SC roda em Kubernetes | Instalação pesada; não cabe junto com as outras VMs no servidor de teste | Rodar um Near-RT RIC de cada vez na Etapa 8 |
| Recursos do host | `ran` + `obs` + `smo` podem passar da capacidade do servidor | Habilitar só as VMs da etapa em uso (`config.yml`) |
| SMO da O-RAN SC pesado | Instalação longa e frágil | Usar só os componentes necessários (A1 PMS, OAM) |
| Y1 sem implementação de referência | A API própria pode divergir da especificação | Seguir o Y1AP e documentar cada desvio |
| RFsimulator limita a vazão pela CPU | Valores absolutos não representam rádio real | Analisar variações relativas entre slices |
