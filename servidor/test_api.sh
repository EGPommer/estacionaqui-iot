#!/bin/bash
# Uso: API_KEY=suachave ./test_api.sh https://USUARIO.pythonanywhere.com
URL="$1"
echo "--- sem chave (esperado 401)"
curl -s -o /dev/null -w "%{http_code}\n" -X POST "$URL/api/leituras" -H "Content-Type: application/json" -d '{"id":"A1","status":"livre","distancia_cm":200}'
echo "--- chave errada (esperado 401)"
curl -s -o /dev/null -w "%{http_code}\n" -X POST "$URL/api/leituras" -H "X-API-Key: errada" -H "Content-Type: application/json" -d '{"id":"A1","status":"livre","distancia_cm":200}'
echo "--- payload invalido (esperado 400)"
curl -s -o /dev/null -w "%{http_code}\n" -X POST "$URL/api/leituras" -H "X-API-Key: $API_KEY" -H "Content-Type: application/json" -d '{"id":"A1","status":"quebrada","distancia_cm":200}'
echo "--- payload valido (esperado 201)"
curl -s -o /dev/null -w "%{http_code}\n" -X POST "$URL/api/leituras" -H "X-API-Key: $API_KEY" -H "Content-Type: application/json" -d '{"id":"A1","status":"ocupada","distancia_cm":23.4,"timestamp":"2026-11-15T14:30:00-03:00"}'
