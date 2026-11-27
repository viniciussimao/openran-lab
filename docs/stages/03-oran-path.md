# Etapa 3: caminho O-RAN com FlexRIC

**Status:** em investigação. Este documento reúne o que a leitura das especificações e do código dos componentes mostrou e propõe um desenho, que ainda precisa ser aprovado. Nada foi instalado nem testado no servidor.

**Objetivo:** fechar o laço O-RAN completo, intenção → rApp → A1 → xApp → E2 → gNB, com componentes prontos da O-RAN SC onde existem e código próprio só onde não existem (decisão 16). Mostrar o gNB no SMO pelo O1.

**Critério de aceite (roadmap):** uma intenção enviada ao rApp vira política A1 e depois ação E2, e a mudança de PRBs e de vazão do slice aparece no KPM e no `iperf3`. O A1 PMS da O-RAN SC opera a terminação A1-P. O gNB aparece no SMO pelo O1.

## Resumo

| # | Pergunta | Proposta |
| --- | --- | --- |
| P1 | Que versão do A1 seguir? | **A1AP v2.0** (`/A1-P/v2`). É a primeira que o A1 PMS tenta ao descobrir um Near-RT RIC, e o A1 Simulator a implementa (`STD_2.0.0`) |
| P2 | Que tipo de política usar? | **`ORAN_SliceSLATarget_2.0.0`**, o tipo padronizado de SLA de slice da A1TD v5 (ETSI TS 103 988, cláusula 7.2.7), com metas de vazão em kbps |
| P3 | Como a meta de vazão vira cota de PRB? | Numa **xApp de SLA de slice em malha fechada**: lê a vazão de cada slice pelo KPM e ajusta `min_ratio` e `max_ratio` pelo E2SM-RC, reaproveitando o núcleo das duas xApps da Etapa 2 |
| P4 | Como fazer a terminação A1-P? | **Serviço próprio na VM `ran`**, separado da xApp. Ele implementa os endpoints do A1AP v2.0 e repassa as políticas à xApp num formato igual ao do A1 Mediator da O-RAN SC, para a troca na Etapa 8 |
| P5 | Como rodar o Non-RT RIC? | **A1 PMS e A1 Simulator da release N da O-RAN SC, em Docker**, na VM `smo`, sem Kubernetes, com as imagens fixadas por digest |
| P6 | Como fazer o rApp de SLA? | **Serviço próprio na VM `smo`**, com uma API de intenção que cria, atualiza e apaga políticas A1 pela API northbound do A1 PMS e desfaz a intenção no fim do prazo |
| P7 | Como fazer o O1? | **Módulo O1 do telnet do OAI + O1 Adapter da OAI + OAM da O-RAN SC em Docker** (SDN-R, ODLUX e VES Collector). É a parte mais pesada e menos madura e fica por último |
| P8 | Como as VMs se falam? | **Uma rede privada com IPs fixos**, definida no `config.yml`, além da rede de gerenciamento do Vagrant |

Os detalhes, as evidências e as alternativas de cada proposta estão nas seções abaixo. A arquitetura proposta e o plano em passos estão no fim.

## P1. Versão do A1: A1AP v2.0

O A1 PMS da O-RAN SC vem do projeto `ccsdk/oran` da ONAP. Ao conhecer um Near-RT RIC, ele descobre a versão do A1 tentando cada uma, nesta ordem (`A1ClientFactory.getProtocolVersion()`):

1. `STD_V2_0_0`: `GET {baseUrl}/A1-P/v2/policytypes`;
2. `STD_V1_1`;
3. `OSC_V1`;
4. as variantes pelo controlador A1 do SDNC.

Para o `STD_V2_0_0`, o PMS usa estes endpoints do Near-RT RIC (`StdA1ClientVersion2`):

| Método e caminho (base `{baseUrl}/A1-P/v2`) | Uso pelo PMS |
| --- | --- |
| `GET /policytypes` | Descobrir a versão e listar os tipos |
| `GET /policytypes/{policyTypeId}` | Ler o `policySchema` do tipo (o PMS lê esse campo do `PolicyTypeObject`) |
| `GET /policytypes/{policyTypeId}/policies` | Listar as políticas de um tipo |
| `PUT /policytypes/{policyTypeId}/policies/{policyId}[?notificationDestination=...]` | Criar ou atualizar uma política |
| `DELETE /policytypes/{policyTypeId}/policies/{policyId}` | Apagar uma política |
| `GET /policytypes/{policyTypeId}/policies/{policyId}/status` | Ler o estado da política |

O A1 Simulator da O-RAN SC (`sim/a1-interface`, tag 2.8.2) implementa as mesmas três versões (`OSC_2.1.0`, `STD_1.1.3` e `STD_2.0.0`). A OpenAPI que ele usa para a `STD_2.0.0` é a do O-RAN.WG2.A1AP-v02.00 (`ORAN_A1-p_V2.0.0_api.yaml`).

**Por que a v2.0:** é a versão padronizada que o PMS tenta primeiro e a única padronizada com tipos de política (a `STD_1.1.3` não tem). O simulador serve de referência de comportamento antes de a terminação própria existir.

**Cuidado com a sincronização:** o PMS sincroniza o estado do Near-RT RIC e pode apagar, no RIC, políticas que ele não conhece (`deleteAllPolicies`). Por isso, a terminação não guarda políticas criadas por fora do PMS.

## P2. Tipo de política: `ORAN_SliceSLATarget_2.0.0`

A A1TD v5 (ETSI TS 103 988 V5.0.0) define sete tipos de política. O único de slice é o **`ORAN_SliceSLATarget_2.0.0`** (cláusula 7.2.7, caso de uso "RAN Slice SLA Assurance"). Nenhum tipo padronizado traz cotas de PRB (`RRMPolicyRatio`).

Estrutura da política, resumida do esquema da cláusula 7.2.7.3.1:

```json
{
  "scope": {
    "sliceId": { "sst": 1, "sd": "000001", "plmnId": { "mcc": "001", "mnc": "01" } }
  },
  "sliceSlaObjectives": {
    "guaDlThptPerSlice": 30000,
    "maxDlThptPerSlice": 60000
  }
}
```

- **`scope.sliceId`** identifica o slice: `sst` (0 a 255), `sd` (6 dígitos hexadecimais, opcional) e `plmnId` (`mcc` com 3 dígitos e `mnc` com 2 ou 3).
- **`sliceSlaObjectives`** tem as metas, todas opcionais (pelo menos uma): vazão garantida e máxima por slice e por UE, DL e UL, em **kbps**; número máximo de UEs e de sessões PDU; atraso, perda, jitter e prioridade do slice.
- **Estado da política:** o esquema genérico da cláusula 7.1.2.2, com `enforceStatus` (`ENFORCED` ou `NOT_ENFORCED`) e, quando não aplicada, `enforceReason` (`SCOPE_NOT_APPLICABLE`, `STATEMENT_NOT_APPLICABLE` ou `OTHER_REASON`).

A especificação deixa em aberto como o Near-RT RIC usa as metas de vazão por slice (NOTA 2 da tabela de `SliceSlaObjectives`: "for further study" no WG3). A interpretação do laboratório vai ficar documentada (P3).

**Alternativa considerada:** um tipo próprio com as cotas de PRB (`min_ratio`, `max_ratio`, `dedicated_ratio`). Seria mais simples, porque bastaria repassar as cotas à `slice_quota_ctrl`. Fica descartado: não é padrão, e o artigo 1 ganha mais com o tipo padronizado, que o rApp e o PMS de qualquer fornecedor entendem. As cotas continuam sendo o mecanismo de controle, mas só do A1 para baixo (E2).

**Escopo da Etapa 3:** as metas de vazão DL por slice (`guaDlThptPerSlice` e `maxDlThptPerSlice`), que o escalonador por slice consegue cumprir. As outras metas recebem `NOT_ENFORCED` com `STATEMENT_NOT_APPLICABLE`.

## P3. xApp de SLA de slice

A política fala em vazão (kbps), e o gNB entende cotas de PRB. A relação entre as duas muda com o canal, o número de UEs e a carga da CPU no rfsim. Por isso, a tradução fica numa malha fechada:

1. A cada período (proposta: 1 s, o mesmo do KPM), a xApp lê a vazão DL de cada slice (`DRB.UEThpDl`, como a `slice_kpm_monitor`).
2. Para cada slice com política:
   - **`maxDlThptPerSlice`:** se a vazão passa da meta, baixa o `max_ratio`; se fica bem abaixo e o slice tem demanda, sobe;
   - **`guaDlThptPerSlice`:** se a vazão fica abaixo da meta e o slice tem demanda, sobe o `min_ratio`, até a soma dos mínimos chegar a 100%.
3. Manda as cotas novas pelo E2SM-RC (como a `slice_quota_ctrl`), só quando mudam, com histerese para não oscilar.
4. Informa o estado da política à terminação A1-P: `ENFORCED` quando cumpre a meta; `NOT_ENFORCED` com `OTHER_REASON` quando não consegue (por exemplo, garantias que somam mais que a célula).

O núcleo da xApp (controle e estado das políticas) não depende de RIC, como nas xApps da Etapa 2, e reaproveita o `skm_core` e o `sqc_core`.

**Pontos a decidir no desenho detalhado:** o ganho e a histerese do controle; o que fazer com um slice sem demanda (não subir o mínimo à toa); e a diferença entre a vazão do KPM e a do `iperf3` com saturação (K3 da Etapa 2: o KPM mede o que o RLC do gNB transmite).

## P4. Terminação A1-P

O FlexRIC não termina o A1 (decisão 16). A terminação é a única peça própria do lado do Near-RT RIC.

**Proposta: um serviço REST separado, na VM `ran`.**

- **Endpoints:** os seis da tabela da P1, seguindo a OpenAPI do A1AP v2.0, mais o `POST` de notificação de estado para o `notificationDestination` que o PMS informar.
- **Tipos de política:** o `ORAN_SliceSLATarget_2.0.0`, com o esquema da A1TD. Toda política é validada contra o esquema antes de ser aceita (`400` se não passar).
- **Persistência:** as políticas ficam num arquivo, para sobreviver a um reinício do serviço. O PMS sincroniza o resto.
- **Ligação com a xApp:** a terminação repassa cada política à xApp de SLA por uma conexão local e recebe dela o estado. A mensagem segue o formato JSON que o A1 Mediator da O-RAN SC entrega às xApps (`operation`, `policy_type_id`, `policy_instance_id` e `payload`). Na Etapa 8, a xApp troca a origem das políticas sem mudar a lógica (decisão 17).
- **Linguagem:** Python, com um framework que gere a validação a partir da OpenAPI. A xApp continua em C. A terminação não está no caminho crítico do laço (uma política por intenção), e o Python encurta o desenvolvimento e os testes de conformidade.

**Alternativa considerada:** um servidor HTTP dentro do processo da xApp, em C. Fica descartada: misturaria o A1 com a lógica da xApp, o que dificulta a troca pelo A1 Mediator na Etapa 8 e os testes da terminação sozinha.

**Conformidade:** antes de ligar a terminação ao PMS, ela passa pelos mesmos fluxos que o A1 Simulator (`STD_2.0.0`) com o PMS. Depois, troca-se o simulador pela terminação, sem mudar o PMS.

## P5. Non-RT RIC na VM `smo`

- **Componentes:** só o A1 Policy Management Service e o A1 Simulator. O painel de controle da O-RAN SC e o controlador A1 do SDNC não são necessários: o PMS fala HTTP direto com o Near-RT RIC.
- **Versões:** release N da O-RAN SC. As páginas de execução em Docker da release N usam `nonrtric-plt-a1policymanagementservice:2.13.0` e `a1-simulator:2.8.0`. O repositório do PMS já está em `2.14.0-SNAPSHOT`, e o do simulador, na tag 2.8.2. As imagens exatas são escolhidas e fixadas por digest na implementação, como as do core (decisão 9).
- **Configuração:** os Near-RT RICs vão no `application_configuration.json` (`name`, `baseUrl` e `managedElementIds`). O PMS também aceita a troca da configuração com ele no ar (`/configuration` na API northbound).
- **API northbound:** a v3 (`/a1-policy-management/v1`) para o rApp, com `policies`, `policy-types`, `rics` e `services`. A v2 (`/a1-policy/v2`) continua disponível.
- **Execução:** Docker Compose numa role nova, depois de validado à mão.

## P6. rApp de SLA

**Proposta: um serviço próprio na VM `smo`**, em Python, com:

- **API de intenção:** uma intenção diz o slice, a meta (por exemplo, "mais banda": `maxDlThptPerSlice` maior) e o prazo. O botão da Etapa 6 vai chamar essa mesma API.
- **Tradução:** cada intenção vira uma política `ORAN_SliceSLATarget_2.0.0`, criada pela API northbound do PMS.
- **Prazo:** no fim do prazo, o rApp apaga a política ou volta à anterior.
- **Regras:** recusar intenções que firam o mínimo garantido de outro slice (o teste negativo do experimento final).

**Alternativa considerada:** o gerenciador de rApps da O-RAN SC (R1). Fica para depois: ele acrescenta Kubernetes e empacotamento sem mudar o laço. O desvio fica documentado.

## P7. O1

**O que existe:**

- O OAI `2026.w46` tem um módulo O1 no servidor telnet do gNB (`telnetsrv_o1.c`), com os comandos `stats`, `config`, `bwconfig`, `stop_modem`, `start_modem` e `remove_mac_ues`. Ele é ligado com `--telnetsrv --telnetsrv.listenport 9091 --telnetsrv.shrmod o1`.
- O **O1 Adapter** da OAI (`gitlab.eurecom.fr/oai/o1-adapter`, Apache-2.0) faz a ponte entre esse telnet e o SMO: é um servidor NETCONF (YANG da 3GPP, por exemplo `_3gpp-nr-nrm-gnbdufunction`) e manda eventos VES (`pnfRegistration` e alarmes) ao VES Collector. O repositório não tem tags, então a versão é fixada por commit. Nenhuma fonte diz com que versão do OAI ele foi testado.
- O **OAM da O-RAN SC** (repositório `oam`, pasta `solution`) roda em Docker Compose. A parte de OAM (`solution/smo/oam`) tem o controlador SDN-R (`sdnr-image:13.0.1`), a interface ODLUX e o VES Collector (`vescollector:1.12.5`). Ela depende da parte comum (`solution/smo/common`): Traefik, Keycloak com PostgreSQL, MariaDB, Zookeeper, Kafka e o servidor de topologia. A solução foi testada com 4 núcleos, 16 GB de RAM e 50 GB de disco. Há também uma versão para Kubernetes (`solution_k8s`), mais pesada (20 a 32 GB).

**Fluxo esperado:** o O1 Adapter manda um `pnfRegistration` ao VES Collector. O SDN-R lê o evento no Kafka e abre uma sessão NETCONF com o adapter (porta 1830 nos exemplos). O gNB aparece no ODLUX.

**Problema conhecido:** o adapter manda o `NotifyFileReady` no esquema da 3GPP Release 18, e o VES Collector só valida até a Release 16 (erro HTTP 400). A solução provisória, nos guias da comunidade, é desligar a validação de esquema externo do coletor.

**Proposta:** o O1 fica no fim da etapa, num passo independente do laço do A1. Ele roda o OAM em Docker (sem Kubernetes) na VM `smo`, mas só durante os próprios testes, por causa da memória (veja "Recursos").

## P8. Rede entre as VMs

Hoje cada VM só tem a rede de gerenciamento do vagrant-libvirt, com IP por DHCP. O A1 e o O1 precisam de endereços estáveis: a `baseUrl` do Near-RT RIC no PMS, a URL do VES no O1 Adapter e a URL do PMS no rApp.

**Proposta:** uma rede privada do libvirt, só entre as VMs, com IP fixo por VM. A sub-rede e os IPs ficam no `config.yml` (decisão 7: nada que varie entre servidores fica fixo no código). O Ansible lê os IPs de lá para montar as URLs.

## Recursos

| Item | Valor |
| --- | --- |
| Servidor de teste | 12 CPUs, 30 GB de RAM, compartilhado |
| VM `ran` hoje | 8 vCPU e 16 GB. Uso medido com os dois UEs saturados: CPU em 93%, memória em 5,3 GB |
| VM `smo` com o A1 PMS e o rApp | Proposta inicial: 2 a 4 vCPU e 4 a 6 GB, a medir |
| OAM da O-RAN SC | 4 núcleos e 16 GB, pela documentação dele |

O laço do A1 cabe: `ran` com 8 vCPU e 16 GB, mais a `smo` com 4 vCPU e 6 GB, dá 22 GB. Com o OAM, não cabe sem ajuste. A proposta é baixar a memória da VM `ran` (que usa 5,3 GB) para 10 GB durante os testes do O1 e dar à `smo` o que o OAM pedir, medindo antes. Se não couber, o O1 roda num momento em que a VM `ran` está só com o gNB e um UE.

## Arquitetura proposta

```
VM smo                                         VM ran
  rApp de SLA (próprio)                          terminação A1-P (própria, A1AP v2.0)
    API de intenção  ◄── curl (Etapa 3)            │ políticas e estados (formato do A1 Mediator)
    │ API northbound v3                            ▼
  A1 PMS (O-RAN SC) ── A1 (HTTP, A1-P/v2) ──►    xApp de SLA de slice (própria, C)
  A1 Simulator (testes)                            │ E2SM-KPM (vazão)   │ E2SM-RC (cotas)
  OAM: SDN-R, ODLUX, VES (só no passo do O1)        ▼                     ▼
    ▲ VES      │ NETCONF                         nearRT-RIC (FlexRIC) ── E2 ──► gNB do fork
    └────────── O1 Adapter (OAI) ◄── telnet (módulo o1) ──────────────────────────┘
```

## Plano em passos

Cada passo tem o próprio critério e segue a regra "manual primeiro, automação depois".

| Passo | Entrega | Critério |
| --- | --- | --- |
| 3.1 Rede e VM `smo` | Rede privada com IPs fixos; VM `smo` com Docker | `ran` e `smo` se alcançam pelos IPs fixos; o `vagrant provision` da `ran` não muda nada (`changed=0`) |
| 3.2 Non-RT RIC com o simulador | A1 PMS e A1 Simulator (`STD_2.0.0`) na `smo` | O PMS descobre o simulador como `STD_V2_0_0`, sincroniza o tipo `ORAN_SliceSLATarget_2.0.0` e cria, lê e apaga uma política pela API v3 |
| 3.3 Terminação A1-P | Serviço próprio na `ran`, com testes | Os mesmos fluxos do 3.2, trocando o simulador pela terminação; uma política inválida recebe `400` |
| 3.4 xApp de SLA de slice | Política A1 → malha fechada → cotas pelo E2SM-RC | Uma política com `maxDlThptPerSlice` limita a vazão do slice (KPM e `iperf3`); uma com `guaDlThptPerSlice` garante a vazão sob disputa; o estado volta ao PMS |
| 3.5 rApp de SLA | API de intenção na `smo` | Uma intenção por `curl` muda a vazão do slice até o gNB e é desfeita no fim do prazo; uma intenção que fere o mínimo de outro slice é recusada |
| 3.6 O1 | Módulo O1 do gNB, O1 Adapter e OAM | O gNB aparece no ODLUX pelo NETCONF; o `pnfRegistration` chega ao VES Collector |

Depois do 3.6, as decisões tomadas viram entradas novas no [registro de decisões](../decisions.md), e o passo a passo de cada parte entra neste guia.

## A confirmar antes ou durante a implementação

- **Recursos livres no servidor:** como ele é compartilhado, confirmar quanta CPU e memória sobram com a VM `ran` no ar (`free -g`, `nproc`, `virsh list`).
- **Imagens da release N:** escolher as tags do A1 PMS e do A1 Simulator e fixar os digests.
- **O1 Adapter com o OAI `2026.w46`:** confirmar que o adapter entende a saída do módulo `o1` do telnet desta tag. Nenhuma fonte documenta a versão testada.
- **Tamanho do OAM em Docker:** medir a memória real da `solution/smo` (common + oam) antes de decidir os recursos da `smo`.
- **Unidade e base da vazão:** confirmar, com o simulador, se o PMS e o rApp usam kbps de ponta a ponta, como diz a A1TD.

## Fontes

- **A1 PMS:** código do [`ccsdk/oran`](https://github.com/onap/ccsdk-oran) (commit `9cb1e4e`, 2026-10-16): `A1ClientFactory.java`, `StdA1ClientVersion2.java` e a API northbound `pms-api-v3.yaml`. Repositório da O-RAN SC: [`nonrtric-plt-a1policymanagementservice`](https://github.com/o-ran-sc/nonrtric-plt-a1policymanagementservice) (commit `b7f4cb3`, versão `2.14.0-SNAPSHOT`).
- **A1 Simulator:** [`sim-a1-interface`](https://github.com/o-ran-sc/sim-a1-interface) (commit `9e15e3f`, tag 2.8.2), OpenAPI `api/STD_2.0.0/ORAN_A1-p_V2.0.0_api.yaml`.
- **Release N em Docker:** páginas [Release N - Run in Docker](https://lf-o-ran-sc.atlassian.net/wiki/spaces/RICNR/pages/929792252) e [Release N - Build](https://lf-o-ran-sc.atlassian.net/wiki/spaces/RICNR/pages/929792205) da wiki do Non-RT RIC.
- **A1TD:** [ETSI TS 103 988 V5.0.0](https://www.etsi.org/deliver/etsi_ts/103900_103999/103988/05.00.00_60/ts_103988v050000p.pdf) (O-RAN A1 Type Definitions), cláusulas 6.3.3.5, 7.1.2.2 e 7.2.7.
- **OAM:** repositório [`oam`](https://github.com/o-ran-sc/oam) da O-RAN SC (commit `9b2dfe6`), `solution/README.md` e `solution/smo/{common,oam}/.env`.
- **O1:** [`oai/o1-adapter`](https://gitlab.eurecom.fr/oai/o1-adapter); `common/utils/telnetsrv/telnetsrv_o1.c` do OAI `2026.w46`; guias da comunidade [OAI O1 Adapter](https://docs.ngkorefoundation.org/tutorials/oai/oai-o1-adapter/) e [OSC OAM](https://github.com/ngkore/OSC-OAM), que não são documentação oficial.
