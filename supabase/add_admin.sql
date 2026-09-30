-- Donne les droits admin à un compte Supabase Auth.
-- 1. Crée d'abord l'utilisateur : Authentication → Users → Add user → Create new user
--    (email + mot de passe, coche « Auto Confirm User »).
-- 2. Remplace l'email ci-dessous puis exécute ce script dans le SQL Editor.

insert into public.admins (user_id)
select id from auth.users where email = 'ton-email@exemple.com'
on conflict do nothing;

-- Vérification : doit afficher une ligne
select a.user_id, u.email from public.admins a join auth.users u on u.id = a.user_id;
