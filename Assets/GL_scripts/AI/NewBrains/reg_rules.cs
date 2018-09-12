using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Отсюда вызываются все правила, формируют список решений с весами 
/// </summary>
public static class reg_rules {


	//Каждое правило генерирует список решений, исходя из данных объекта командования
	//принимающего решение. Вероятности суммируются и принимается решение
	public static int des (int reg_num)
	{
		List <des_point> res = new List<des_point> ();

		/// Это прописать по количеству готовых правил
		/// ---
		List <des_point> temp = rule1.rule (reg_num);
		res = sum (res, temp);
		/// ---
		return Decision.MyMakeDecision (res);
	}



	public static List <des_point> sum (List <des_point> x, List <des_point> y)
	{
		/// Ищем в списке y все решения которые уже есть в списке x 
		for (int i = 0; i < x.Count; i++) {
			for (int h = (i + 1); h < y.Count; h++) {
				if (x [i].num_des == y [h].num_des && y [h].weight != 0) {
					x[i].putweight ( x [i].weight +   y [h].weight);
					y [h].putweight ( 0);
				}
			}
		}
		/// добавляем в список x решения из y, которые останутся неприкаянными

		for (int i = 0; i < y.Count; i++) {
			if (y [i].weight != 0) {
				des_point temp = new des_point ();
				temp = y [i];
				x.Add (temp);
			}
		}

		return x;
	}




}
