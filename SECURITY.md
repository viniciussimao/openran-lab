# Política de segurança

## Escopo

O openran-lab é um ambiente de laboratório para estudo e pesquisa. Ele **não deve ser usado em redes de produção** nem exposto à internet.

## Boas práticas do projeto

- Nenhum segredo, chave privada ou certificado vai para o repositório. Certificados (por exemplo, os do mTLS da interface Y1) são gerados localmente durante o provisionamento.
- Arquivos de configuração por servidor (`config.yml`) ficam fora do Git.
- As credenciais padrão dos componentes de terceiros (core 5G, SMO, Grafana) são de laboratório e devem ser trocadas se o ambiente for acessível por outras pessoas.

## Como relatar uma vulnerabilidade

Não abra uma issue pública. Use o recurso **Report a vulnerability** (aba *Security* do repositório no GitHub) para enviar um relato privado, com os passos para reproduzir e o impacto esperado.

Vulnerabilidades no OAI, no OAI CN5G ou no FlexRIC devem ser relatadas aos projetos de origem.
