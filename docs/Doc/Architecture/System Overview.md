# Обзор архитектуры

Система состоит из нескольких сервисов, общающихся через KeyDB: API Gateway, воркеры-симуляторы, хранилище данных и внешние клиенты. 

## Компоненты

- Клиент (браузер, сервис или разработчик с GDB) обращается к [[Components/API Gateway|API Gateway]] по REST и SSE. 
- Gateway валидирует запрос, пишет аудит в PostgreSQL и планирует задачу в KeyDB. 
- [[Components/Worker|Воркеры]] забирают задания из очереди KeyDB и запускают симуляцию с/без GDB. 
- События выполнения публикуются через Pub/Sub KeyDB и транслируются клиенту по SSE. 
- [[Components/Storage (Postgres)|PostgreSQL]] хранит историю задач и GDB-сессий. 

## Схема компонентов

(упрощенная текстовая диаграмма)

- Client → API Gateway → PostgreSQL  
- API Gateway ↔ KeyDB  
- Workers ↔ KeyDB  
- GDB-клиент ↔ Worker (GDB-сервер) 

## Особенности

- Gateway не выполняет симуляцию и не участвует в GDB-протоколе. 
- Все состояние задач и маршрутизация между компонентами основаны на KeyDB. 
- Воркеры запускаются в нескольких экземплярах и конкурируют за задачи в очереди. 

Детальная схема GDB-архитектуры — в [[Architecture/GDB Architecture|GDB Architecture]].  
Последовательность действий при отладке — в [[Architecture/Sequence With Debug|Sequence With Debug]]. 